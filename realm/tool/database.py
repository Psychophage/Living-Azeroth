#!/usr/bin/env python3
"""Database work for one realm. Runs inside the realm's tool container (see compose.yaml)."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import uuid

import pymysql

sys.path.insert(0, str(Path(__file__).resolve().parent))
from config import LEDGER_SCHEMA, SCHEMAS, read_state, write_state  # noqa: E402

REALM = Path("/realm")
SOURCE = Path("/source")
PLAYERBOTS_SQL = SOURCE / "modules/mod-playerbots/data/sql/playerbots"
CHARACTER_UPDATES = SOURCE / "modules/mod-living-characters/data/sql/characters/updates"
CHARACTER_MIGRATIONS = ("rev_1790610292455966174.sql", "rev_1790672429868183365.sql", "rev_1790678345515806605.sql")
# Prepared identities for the Northshire knowledge pilot, and the guards that keep watch.
NAMED_NPCS = [
    (197, "npc:marshal-mcbride", "Marshal McBride serves at Northshire Abbey."),
    (295, "npc:innkeeper-farley", "Innkeeper Farley tends the Lion's Pride Inn in Goldshire."),
]
# AzerothCore's SRP6 parameters (Crypto::SRP6).
SRP_GENERATOR = 7
SRP_MODULUS = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)


def password():
    return (REALM / "secrets/database-password").read_text().strip()


def connect(schema=None):
    return pymysql.connect(host="database", user="root", password=password(), database=schema,
                           charset="utf8mb4", autocommit=False)


def statements(path):
    text = "\n".join(line for line in path.read_text().splitlines() if not line.lstrip().startswith("--"))
    return [statement for statement in text.split(";") if statement.strip()]


def run_sql_file(schema, path):
    with path.open("rb") as sql:
        subprocess.run(["mysql", "--host=database", "--user=root", schema], stdin=sql, check=True,
                       env={**os.environ, "MYSQL_PWD": password()})


def apply_once(cursor, schema, path):
    """Apply one additive migration to a schema, recording it so it never runs twice."""
    cursor.execute(f"CREATE TABLE IF NOT EXISTS `{schema}`.living_azeroth_migration "
                   "(name VARCHAR(190) PRIMARY KEY, applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)")
    cursor.execute(f"SELECT 1 FROM `{schema}`.living_azeroth_migration WHERE name=%s", (path.name,))
    if cursor.fetchone():
        return False
    cursor.execute(f"USE `{schema}`")
    for statement in statements(path):
        cursor.execute(statement)
    cursor.execute(f"INSERT INTO `{schema}`.living_azeroth_migration (name) VALUES (%s)", (path.name,))
    return True


def init(budget_dollars):
    """Create or upgrade every schema. Safe to run again; existing rows are never replaced."""
    subprocess.run([str(REALM / "install/bin/dbimport"), "-c", str(REALM / "config/dbimport.conf")], check=True)
    with connect() as connection, connection.cursor() as cursor:
        cursor.execute(f"CREATE DATABASE IF NOT EXISTS `{SCHEMAS['Playerbots']}` CHARACTER SET utf8mb4")
        for source in sorted((PLAYERBOTS_SQL / "base").glob("*.sql")):
            cursor.execute("SELECT 1 FROM information_schema.tables WHERE table_schema=%s AND table_name=%s",
                           (SCHEMAS["Playerbots"], source.stem))
            if not cursor.fetchone():
                run_sql_file(SCHEMAS["Playerbots"], source)
        # The world server's Playerbots updater applies data/sql/playerbots/updates at startup.
        cursor.execute(f"CREATE DATABASE IF NOT EXISTS `{LEDGER_SCHEMA}` CHARACTER SET utf8mb4")
        for name in CHARACTER_MIGRATIONS:
            for schema in (SCHEMAS["Character"], LEDGER_SCHEMA):
                if apply_once(cursor, schema, CHARACTER_UPDATES / name):
                    print("Applied", name, "to", schema)
        connection.commit()
        ensure_budget(cursor, budget_dollars)
        connection.commit()
        write_npc_identities(cursor)


def ensure_budget(cursor, dollars):
    """Create this realm's spending budget once. An existing budget is never changed."""
    state = read_state(REALM)
    identifier = state.get("budget_id")
    if identifier:
        cursor.execute(f"SELECT 1 FROM `{LEDGER_SCHEMA}`.pbc_api_budget WHERE budget_id=%s", (identifier,))
        if cursor.fetchone():
            return
        raise SystemExit(f"Budget {identifier} is missing from the ledger; refusing to create a new one.")
    identifier = "realm-" + str(uuid.uuid4())
    ceiling = int(round(float(dollars) * 1_000_000_000))
    cursor.execute(f"INSERT INTO `{LEDGER_SCHEMA}`.pbc_api_budget (budget_id,ceiling_nano,background_floor_nano) "
                   "VALUES (%s,%s,%s)", (identifier, ceiling, ceiling // 20))
    state["budget_id"] = identifier
    write_state(REALM, state)
    print(f"Created a ${float(dollars):.2f} spending budget.")


def write_npc_identities(cursor):
    world = SCHEMAS["World"]
    mappings = []
    for entry, identifier, canon in NAMED_NPCS:
        cursor.execute(f"SELECT guid,map FROM `{world}`.creature WHERE id=%s AND map=0 AND phaseMask=1 "
                       "ORDER BY guid LIMIT 1", (entry,))
        spawn = cursor.fetchone()
        if spawn:
            mappings.append({"spawn_id": spawn[0], "map_id": spawn[1], "instance_id": 0,
                             "actor_id": identifier, "canon": canon})
    cursor.execute(f"SELECT guid,id,position_x,position_y FROM `{world}`.creature "
                   "WHERE id IN (68,1423,1642) AND map=0 AND phaseMask=1")
    for guid, entry, x, y in cursor.fetchall():
        watch = "northshire" if entry == 1642 else (
            "goldshire" if -9600 < x < -9200 and -250 < y < 150 else (
                "stormwind" if -9200 < x < -8000 and 150 < y < 1600 else None))
        if watch:
            mappings.append({"spawn_id": guid, "map_id": 0, "instance_id": 0, "watch_id": "watch:" + watch})
    (REALM / "config/npcs.json").write_text(json.dumps(mappings, indent=2) + "\n")


def sync_realm(name, address, port):
    with connect(SCHEMAS["Login"]) as connection, connection.cursor() as cursor:
        cursor.execute("UPDATE realmlist SET name=%s,address=%s,localAddress=%s,port=%s WHERE id=1",
                       (name, address, address, int(port)))
        connection.commit()


def create_account(name, gm_level):
    username = name.upper()
    secret = os.environ.pop("LA_ACCOUNT_PASSWORD").upper()
    salt = secrets.token_bytes(32)
    identity = hashlib.sha1(f"{username}:{secret}".encode()).digest()
    exponent = int.from_bytes(hashlib.sha1(salt + identity).digest(), "little")
    verifier = pow(SRP_GENERATOR, exponent, SRP_MODULUS).to_bytes(32, "little")
    with connect(SCHEMAS["Login"]) as connection, connection.cursor() as cursor:
        cursor.execute("SELECT id FROM account WHERE username=%s", (username,))
        row = cursor.fetchone()
        if row:
            cursor.execute("UPDATE account SET salt=%s,verifier=%s WHERE id=%s", (salt, verifier, row[0]))
            account = row[0]
            print(f"Updated the password of {username}.")
        else:
            cursor.execute("INSERT INTO account (username,salt,verifier,expansion) VALUES (%s,%s,%s,2)",
                           (username, salt, verifier))
            account = cursor.lastrowid
            print(f"Created account {username}.")
        if gm_level is not None:
            cursor.execute("REPLACE INTO account_access (id,gmlevel,RealmID,comment) VALUES (%s,%s,-1,%s)",
                           (account, gm_level, "living-azeroth"))
        connection.commit()


def grant_ledger(user):
    """Let a realm that shares this realm's budget use only the two ledger tables it needs."""
    secret = os.environ.pop("LA_LEDGER_PASSWORD")
    with connect() as connection, connection.cursor() as cursor:
        cursor.execute("CREATE USER IF NOT EXISTS %s@'%%' IDENTIFIED BY %s", (user, secret))
        cursor.execute("ALTER USER %s@'%%' IDENTIFIED BY %s", (user, secret))
        for table in ("pbc_api_budget", "pbc_api_request"):
            cursor.execute(f"GRANT SELECT, INSERT, UPDATE ON `{LEDGER_SCHEMA}`.`{table}` TO %s@'%%'", (user,))
        connection.commit()


def show_budget():
    identifier = read_state(REALM).get("budget_id")
    with connect(LEDGER_SCHEMA) as connection, connection.cursor() as cursor:
        cursor.execute("SELECT ceiling_nano,spent_nano,held_nano FROM pbc_api_budget WHERE budget_id=%s",
                       (identifier,))
        row = cursor.fetchone()
    if not row:
        raise SystemExit("This realm has no budget yet.")
    ceiling, spent, held = (value / 1e9 for value in row)
    print(f"Spent ${spent:.2f} of ${ceiling:.2f} (${held:.2f} reserved for requests in flight).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("init").add_argument("--budget-dollars", default="5")
    sync = commands.add_parser("sync-realm")
    sync.add_argument("name")
    sync.add_argument("address")
    sync.add_argument("port")
    account = commands.add_parser("account")
    account.add_argument("name")
    account.add_argument("--gm", type=int)
    commands.add_parser("budget")
    commands.add_parser("grant-ledger").add_argument("user")
    args = parser.parse_args()
    if args.command == "init":
        init(args.budget_dollars)
    elif args.command == "sync-realm":
        sync_realm(args.name, args.address, args.port)
    elif args.command == "account":
        create_account(args.name, args.gm)
    elif args.command == "grant-ledger":
        grant_ledger(args.user)
    else:
        show_budget()


if __name__ == "__main__":
    main()
