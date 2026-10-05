#!/usr/bin/env python3
"""Read-only views for current PBC records, with legacy history fallback.

Uses the mysql client's existing credentials. PBC_MYSQL_DEFAULTS_FILE may name a
private client option file; PBC_DATABASE selects a schema (default acore_characters).
"""

import argparse
import json
import os
import re
import subprocess


def literal(value):
    return "CONVERT(0x" + value.encode("utf-8").hex() + " USING utf8mb4)"


def query(sql):
    database = os.environ.get("PBC_DATABASE", "acore_characters")
    if not re.fullmatch(r"[A-Za-z0-9_]+", database):
        raise ValueError("Invalid database name")
    command = ["mysql"]
    if path := os.environ.get("PBC_MYSQL_DEFAULTS_FILE"):
        command.append("--defaults-extra-file=" + path)
    command += ["--batch", "--raw", "--skip-column-names", database, "-e", sql]
    return subprocess.check_output(command, text=True).splitlines()


def table_exists(name):
    return bool(query("SELECT 1 FROM information_schema.tables WHERE table_schema=DATABASE() "
                      "AND table_name=" + literal(name)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("view", choices=["history", "info"])
    parser.add_argument("character", help="Character name or current actor ID")
    parser.add_argument("limit", nargs="?", type=int, default=5)
    args = parser.parse_args()
    if not 1 <= args.limit <= 1000:
        parser.error("limit must be between 1 and 1000")
    guid = query("SELECT guid FROM characters WHERE name=" + literal(args.character) + " LIMIT 1")
    actor = "player:" + guid[0] if guid else args.character
    current = table_exists("pbc_actor") and query("SELECT actor_id FROM pbc_actor WHERE actor_id=" + literal(actor))
    if current:
        if args.view == "history":
            sql = ("SELECT JSON_OBJECT('source',CONCAT(o.id,':',o.version),'created_ms',o.created_ms,"
                   "'channel',o.channel,'evidence',o.evidence,'text',o.payload) FROM pbc_observation o "
                   "JOIN pbc_witness w ON w.observation_id=o.id WHERE o.excluded=0 AND w.actor_id=" +
                   literal(actor) + " ORDER BY o.id DESC LIMIT " + str(args.limit))
        else:
            sql = ("SELECT JSON_OBJECT('actor',actor_id,'name',display_name,'version',version,"
                   "'foundation',foundation,'recall',recall) FROM pbc_actor WHERE actor_id=" + literal(actor))
        rows = query(sql)
        for row in reversed(rows) if args.view == "history" else rows:
            print(json.dumps(json.loads(row), ensure_ascii=False, indent=2))
        if args.view == "info":
            for row in query("SELECT JSON_OBJECT('note',CONCAT(id,':',version),'kind',kind,'subject',subject_id,"
                             "'authority',authority,'resolved',resolved,'text',content) FROM pbc_note "
                             "WHERE valid=1 AND actor_id=" + literal(actor) + " ORDER BY id DESC LIMIT 20"):
                print(json.dumps(json.loads(row), ensure_ascii=False, indent=2))
        return
    if not guid:
        raise SystemExit("Character not found.")
    print("Legacy records (not the current character system):")
    if args.view == "history":
        if table_exists("mod_pbc_history_owners"):
            sql = ("SELECT JSON_OBJECT('source',h.id,'text',h.message) FROM mod_pbc_history h "
                   "JOIN mod_pbc_history_owners o ON o.history_id=h.id WHERE o.guid=" + guid[0])
        else:
            sql = ("SELECT JSON_OBJECT('source',h.id,'text',h.message) FROM mod_pbc_chat_history h "
                   "WHERE h.bot_guid=" + guid[0])
        for row in reversed(query(sql + " ORDER BY h.id DESC LIMIT " + str(args.limit))):
            print(json.dumps(json.loads(row), ensure_ascii=False, indent=2))
    else:
        for table, column in [("mod_pbc_memories", "memory_text"), ("mod_pbc_relationships", "relationship_text")]:
            if table_exists(table):
                for row in query("SELECT JSON_OBJECT('table'," + literal(table) + ",'text'," + column +
                                 ") FROM " + table + " WHERE bot_guid=" + guid[0]):
                    print(json.dumps(json.loads(row), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
