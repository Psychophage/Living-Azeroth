"""Shared character-system budget protocol (MySQL DB-API, integer USD nanos).

Every HTTP attempt needs a new reservation and a successful dispatch transition.
Timeouts and unknown usage keep their reservation across process/server restarts.
The C++ transport uses the same tables, lock order and state transitions.
"""

from contextlib import contextmanager
from decimal import Decimal, ROUND_CEILING
import json
import uuid

NANOS_PER_DOLLAR = 1_000_000_000
REASONS = frozenset(("selector", "direct", "ambient", "biography", "memory", "compaction", "evaluation"))


def dollars_to_nanos(value):
    amount = Decimal(str(value))
    if not amount.is_finite() or amount < 0:
        raise ValueError("Cost must be a finite non-negative USD amount")
    return int((amount * NANOS_PER_DOLLAR).to_integral_value(rounding=ROUND_CEILING))


def maximum_cost(input_tokens, output_tokens, input_nano_per_token, output_nano_per_token,
                 request_nano=0):
    values = (input_tokens, output_tokens, input_nano_per_token, output_nano_per_token, request_nano)
    if any(type(value) is not int or value < 0 for value in values):
        raise ValueError("Token limits and maximum prices must be non-negative integers")
    return input_tokens * input_nano_per_token + output_tokens * output_nano_per_token + request_nano


class Budget:
    def __init__(self, connect, budget_id):
        self.connect = connect
        self.budget_id = budget_id

    @contextmanager
    def transaction(self):
        connection = self.connect()
        try:
            with connection.cursor() as cursor:
                cursor.execute("START TRANSACTION")
                yield cursor
            connection.commit()
        except BaseException:
            connection.rollback()
            raise
        finally:
            connection.close()

    def _lock(self, cursor):
        cursor.execute("SELECT ceiling_nano,spent_nano,held_nano,background_floor_nano "
                       "FROM pbc_api_budget WHERE budget_id=%s FOR UPDATE", (self.budget_id,))
        row = cursor.fetchone()
        if row is None:
            raise RuntimeError("Budget must be explicitly provisioned before model use")
        return row

    def reserve(self, maximum_nano, *, model, reason, background=False, request_id=None,
                actor_id="", scene_id="", provider="", map_id=0, instance_id=0, zone_id=0, attempt=1):
        if type(maximum_nano) is not int or maximum_nano <= 0:
            raise ValueError("A positive maximum cost is required, including free routes")
        if reason not in REASONS or not model or attempt not in (1, 2):
            raise ValueError("Invalid request classification")
        request_id = request_id or str(uuid.uuid4())
        with self.transaction() as cursor:
            ceiling, spent, held, floor = self._lock(cursor)
            cursor.execute("SELECT request_id FROM pbc_api_request WHERE request_id=%s", (request_id,))
            if cursor.fetchone() is not None:
                return None  # An operation already reserved must never send a second HTTP request.
            if spent + held + maximum_nano + (floor if background else 0) > ceiling:
                return None
            cursor.execute("INSERT INTO pbc_api_request "
                           "(request_id,budget_id,reason,actor_id,scene_id,model,provider,map_id,"
                           "instance_id,zone_id,attempt,held_nano) VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s)",
                           (request_id, self.budget_id, reason, actor_id, scene_id, model, provider,
                            map_id, instance_id, zone_id, attempt, maximum_nano))
            cursor.execute("UPDATE pbc_api_budget SET held_nano=held_nano+%s WHERE budget_id=%s",
                           (maximum_nano, self.budget_id))
        return request_id

    def dispatch(self, request_id):
        """Returns True once only. Commit this transition BEFORE making the HTTP call."""
        with self.transaction() as cursor:
            self._lock(cursor)
            cursor.execute("UPDATE pbc_api_request SET state='dispatched' "
                           "WHERE request_id=%s AND budget_id=%s AND state='reserved'",
                           (request_id, self.budget_id))
            return cursor.rowcount == 1

    def cancel_unsent(self, request_id):
        """Release only a request proven never dispatched; not valid for HTTP timeouts."""
        with self.transaction() as cursor:
            self._lock(cursor)
            cursor.execute("SELECT held_nano,state FROM pbc_api_request "
                           "WHERE request_id=%s AND budget_id=%s FOR UPDATE", (request_id, self.budget_id))
            row = cursor.fetchone()
            if row is None or row[1] != "reserved":
                return False
            cursor.execute("UPDATE pbc_api_request SET state='cancelled',held_nano=0,actual_nano=0 "
                           "WHERE request_id=%s", (request_id,))
            cursor.execute("UPDATE pbc_api_budget SET held_nano=held_nano-%s WHERE budget_id=%s",
                           (row[0], self.budget_id))
        return True

    def reconcile(self, request_id, actual_nano, *, usage=None, provider_request_id="", latency_ms=None):
        """Record provider-confirmed cost, even for a failed/dropped reply. None stays held."""
        if actual_nano is not None and (type(actual_nano) is not int or actual_nano < 0):
            raise ValueError("Actual cost must be non-negative integer nanodollars")
        with self.transaction() as cursor:
            self._lock(cursor)
            cursor.execute("SELECT held_nano,state,actual_nano FROM pbc_api_request "
                           "WHERE request_id=%s AND budget_id=%s FOR UPDATE", (request_id, self.budget_id))
            row = cursor.fetchone()
            if row is None:
                raise ValueError("Unknown reservation")
            held, state, previous = row
            if state == "reconciled":
                if previous != actual_nano:
                    raise ValueError("Conflicting provider reconciliation")
                return True
            if state != "dispatched":
                raise ValueError("Only dispatched requests may have provider charges")
            if actual_nano is None:
                cursor.execute("UPDATE pbc_api_request SET usage_json=%s,provider_request_id=%s,latency_ms=%s "
                               "WHERE request_id=%s", (json.dumps(usage) if usage is not None else None,
                                                       provider_request_id, latency_ms, request_id))
                return True  # Metadata saved; the unknown charge is still held.
            cursor.execute("UPDATE pbc_api_request SET state='reconciled',held_nano=0,actual_nano=%s,"
                           "usage_json=%s,provider_request_id=%s,latency_ms=%s WHERE request_id=%s",
                           (actual_nano, json.dumps(usage) if usage is not None else None,
                            provider_request_id, latency_ms, request_id))
            # Record unexpected overages honestly; subsequent reservations will fail closed.
            cursor.execute("UPDATE pbc_api_budget SET held_nano=held_nano-%s,spent_nano=spent_nano+%s "
                           "WHERE budget_id=%s", (held, actual_nano, self.budget_id))
        return True

    def delivery(self, request_id, outcome):
        if outcome not in ("delivered", "partial", "cancelled", "failed", "silent", "not_applicable"):
            raise ValueError("Unknown delivery outcome")
        with self.transaction() as cursor:
            cursor.execute("UPDATE pbc_api_request SET delivery=%s WHERE request_id=%s AND budget_id=%s",
                           (outcome, request_id, self.budget_id))

    def totals(self):
        with self.transaction() as cursor:
            ceiling, spent, held, floor = self._lock(cursor)
            return dict(ceiling_nano=ceiling, spent_nano=spent, held_nano=held,
                        background_floor_nano=floor)
