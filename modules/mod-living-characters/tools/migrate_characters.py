#!/usr/bin/env python3
"""Additive PBC history import. Run offline after backup; dry-run by default.

Legacy ownership proves only that an old character context contained a record.
Imported evidence is labelled legacy; this tool never reconstructs missing witnesses,
guesses relationship identities, edits old tables, or overwrites current owner edits.
"""

import argparse
import hashlib
import json
from pathlib import Path

import pymysql


def migrate(connection, cards_directory=None):
    counts = {"actors": 0, "observations": 0, "ownerships": 0, "notes": 0, "cards": 0}
    with connection.cursor(pymysql.cursors.DictCursor) as cursor:
        cursor.execute("SHOW TABLES")
        tables = {next(iter(row.values())) for row in cursor.fetchall()}
        cursor.execute("SELECT guid,name FROM characters")
        characters = {int(row["guid"]): row["name"] for row in cursor.fetchall()}

        def actor(guid):
            identifier = f"player:{guid}"
            cursor.execute("INSERT IGNORE INTO pbc_actor "
                           "(actor_id,kind,display_name,foundation,summary,recall) VALUES (%s,'bot',%s,'','','')",
                           (identifier, characters.get(guid, f"Legacy character {guid}")))
            counts["actors"] += cursor.rowcount
            return identifier

        def observe(key, author, channel, text, milliseconds, witnesses):
            cursor.execute("SELECT id FROM pbc_observation WHERE event_key=%s", (key,))
            found = cursor.fetchone()
            if found:
                identifier = found["id"]
            else:
                cursor.execute("INSERT INTO pbc_observation "
                    "(event_key,scene_id,author_id,channel,map_id,instance_id,zone_id,created_ms,evidence,payload) "
                    "VALUES (%s,%s,%s,%s,0,0,0,%s,'legacy',%s)",
                    (key, hashlib.sha256(key.encode()).hexdigest(), author, channel, milliseconds or 0, text))
                identifier = cursor.lastrowid
                counts["observations"] += 1
            for witness in witnesses:
                # Old context remains readable, but importing the realm's archive
                # must not launch unattended model extraction for every old bot.
                cursor.execute("INSERT IGNORE INTO pbc_witness (actor_id,observation_id,processed_version) "
                               "VALUES (%s,%s,1)",
                               (witness, identifier))
                counts["ownerships"] += cursor.rowcount
                cursor.execute("UPDATE pbc_actor SET last_observed_ms=GREATEST(last_observed_ms,%s) "
                               "WHERE actor_id=%s", (milliseconds or 0, witness))
            return identifier

        def note(key, owner, kind, text, source, milliseconds):
            cursor.execute("INSERT IGNORE INTO pbc_note "
                           "(operation_id,actor_id,kind,content,created_ms) VALUES (%s,%s,%s,%s,%s)",
                           (key, owner, kind, text, milliseconds or 0))
            counts["notes"] += cursor.rowcount
            cursor.execute("SELECT id FROM pbc_note WHERE operation_id=%s AND actor_id=%s", (key, owner))
            identifier = cursor.fetchone()["id"]
            cursor.execute("INSERT IGNORE INTO pbc_note_source (note_id,observation_id,source_version) "
                           "VALUES (%s,%s,1)", (identifier, source))
            cursor.execute("UPDATE pbc_witness SET processed_version=1 WHERE actor_id=%s "
                           "AND observation_id=%s AND processed_version=0", (owner, source))

        # Take copies before issuing writes through the same cursor.
        if {"mod_pbc_history", "mod_pbc_history_owners"} <= tables:
            cursor.execute("SELECT guid,history_id FROM mod_pbc_history_owners ORDER BY history_id,guid")
            owners = {}
            for row in cursor.fetchall():
                owners.setdefault(row["history_id"], []).append(row["guid"])
            cursor.execute("SELECT id,author_guid,type,message,CAST(UNIX_TIMESTAMP(timestamp)*1000 AS UNSIGNED) "
                           "AS milliseconds FROM mod_pbc_history ORDER BY id")
            for row in cursor.fetchall():
                author = f"player:{row['author_guid']}" if row["author_guid"] else "legacy:narrator"
                channel = {0: "legacy_event", 1: "say", 2: "party", 6: "yell", 7: "whisper"}.get(row["type"], "legacy")
                # Preserve the source ID in the event key and leave all old rows intact.
                observe(f"legacy:history:{row['id']}", author, channel, row["message"], row["milliseconds"],
                        [actor(guid) for guid in owners.get(row["id"], [])])
        elif "mod_pbc_chat_history" in tables:
            cursor.execute("SELECT id,bot_guid,message,CAST(UNIX_TIMESTAMP(timestamp)*1000 AS UNSIGNED) "
                           "AS milliseconds FROM mod_pbc_chat_history ORDER BY id")
            for row in cursor.fetchall():
                observe(f"legacy:chat:{row['id']}", "legacy:unknown", "legacy", row["message"],
                        row["milliseconds"], [actor(row["bot_guid"])])

        if "mod_pbc_memories" in tables:
            cursor.execute("SELECT id,bot_guid,memory_text,CAST(UNIX_TIMESTAMP(created_at)*1000 AS UNSIGNED) "
                           "AS milliseconds FROM mod_pbc_memories ORDER BY id")
            for row in cursor.fetchall():
                owner = actor(row["bot_guid"])
                key = f"legacy:memory:{row['id']}"
                text = "Legacy recollection (original evidence unavailable): " + row["memory_text"]
                source = observe(key, owner, "legacy_memory", text, row["milliseconds"], [owner])
                note(key, owner, "memory", text, source, row["milliseconds"])

        if "mod_pbc_relationships" in tables:
            cursor.execute("SELECT bot_guid,target_name,relationship_text,"
                           "CAST(UNIX_TIMESTAMP(updated_at)*1000 AS UNSIGNED) AS milliseconds "
                           "FROM mod_pbc_relationships ORDER BY bot_guid,target_name")
            for row in cursor.fetchall():
                owner = actor(row["bot_guid"])
                key = "legacy:relationship:" + hashlib.sha256(
                    f"{row['bot_guid']}:{row['target_name']}".encode()).hexdigest()
                text = f"Legacy relationship with {row['target_name']} (identity unresolved): " + row["relationship_text"]
                source = observe(key, owner, "legacy_relationship", text, row["milliseconds"], [owner])
                note(key, owner, "relationship", text, source, row["milliseconds"])

        if "mod_pbc_character_card_additions" in tables:
            cursor.execute("SELECT id,bot_guid,addition FROM mod_pbc_character_card_additions ORDER BY id")
            for row in cursor.fetchall():
                owner = actor(row["bot_guid"])
                key = f"legacy:addition:{row['id']}"
                text = "Legacy card addition: " + row["addition"]
                source = observe(key, owner, "legacy_card", text, 0, [owner])
                note(key, owner, "memory", text, source, 0)

        if cards_directory:
            by_name = {name.casefold(): guid for guid, name in characters.items()}
            for path in sorted(Path(cards_directory).glob("*.card.txt")):
                name = path.name.removesuffix(".card.txt")
                if name.casefold() not in by_name:
                    continue
                owner = actor(by_name[name.casefold()])
                text = path.read_bytes().decode("utf-8").replace("\r\n", "\n").strip()
                if not text:
                    continue
                cursor.execute("UPDATE pbc_actor SET foundation=%s,summary=%s,version=version+1 "
                               "WHERE actor_id=%s AND foundation=''", (text, "Existing character card for " + name, owner))
                counts["cards"] += cursor.rowcount
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--connection-file", type=Path, required=True,
                        help="Private file containing host;port;user;password;character_database")
    parser.add_argument("--cards-directory", type=Path)
    parser.add_argument("--apply", action="store_true")
    arguments = parser.parse_args()
    host, port, user, password, database = arguments.connection_file.read_text().strip().split(";", 4)
    connection = pymysql.connect(host=host, port=int(port), user=user, password=password,
                                 database=database, charset="utf8mb4", autocommit=False)
    with connection:
        try:
            connection.begin()
            counts = migrate(connection, arguments.cards_directory)
            if arguments.apply:
                connection.commit()
            else:
                connection.rollback()
            print(json.dumps({"applied": arguments.apply, "new_records": counts}))
        except Exception:
            connection.rollback()
            raise


if __name__ == "__main__":
    main()
