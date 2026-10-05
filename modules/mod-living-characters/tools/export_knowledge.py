#!/usr/bin/env python3
"""Export bounded public world evidence for offline authoring. No model calls or DB writes."""

import argparse
import hashlib
import json
from pathlib import Path


def export(connection, entries):
    """The caller supplies a world-only connection; never reads character histories."""
    placeholders = ','.join(['%s'] * len(entries))
    with connection.cursor() as cursor:
        cursor.execute('START TRANSACTION READ ONLY')
        cursor.execute(f'SELECT entry,name,subname,faction,type,`rank` FROM creature_template '
                       f'WHERE entry IN ({placeholders}) ORDER BY entry', entries)
        creatures = cursor.fetchall()
        if {row['entry'] for row in creatures} != set(entries):
            raise ValueError('One or more requested creature entries do not exist')
        relations = {}
        quests = set()
        for table in ('creature_queststarter', 'creature_questender'):
            cursor.execute(f'SELECT id,quest FROM {table} WHERE id IN ({placeholders}) ORDER BY id,quest', entries)
            relations[table] = cursor.fetchall()
            quests.update(row['quest'] for row in relations[table])
        if len(quests) > 128:
            raise ValueError('Too many linked quests; split the authoring assignment')
        quest_data = {}
        if quests:
            marks = ','.join(['%s'] * len(quests))
            for table in ('quest_template', 'quest_template_addon', 'quest_offer_reward', 'quest_request_items'):
                cursor.execute(f'SELECT * FROM {table} WHERE ID IN ({marks}) ORDER BY ID', sorted(quests))
                quest_data[table] = cursor.fetchall()
        cursor.execute(f'SELECT CreatureID,GroupID,ID,Text,Language FROM creature_text '
                       f'WHERE CreatureID IN ({placeholders}) ORDER BY CreatureID,GroupID,ID', entries)
        speech = cursor.fetchall()
    connection.rollback()
    return {'creatures': creatures, 'quest_relations': relations, 'quests': quest_data, 'scripted_speech': speech}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--connection-file', type=Path, required=True,
                        help='Private JSON with host, port, user, password and database; never included in output')
    parser.add_argument('--entries', required=True, help='1–32 comma-separated creature entry IDs')
    parser.add_argument('--era', required=True)
    parser.add_argument('--revision', required=True, help='World database revision or inspected snapshot identifier')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    entries = sorted(set(int(value) for value in args.entries.split(',')))
    if not entries or len(entries) > 32 or any(value <= 0 for value in entries):
        parser.error('Provide 1–32 positive creature entries')
    if args.output.exists():
        parser.error('Output exists; choose a new evidence file')
    import pymysql
    settings = json.loads(args.connection_file.read_bytes())
    with pymysql.connect(**settings, charset='utf8mb4', cursorclass=pymysql.cursors.DictCursor) as connection:
        evidence = export(connection, entries)
    payload = json.dumps(evidence, ensure_ascii=False, sort_keys=True, default=str).encode()
    bundle = {'version': 1, 'era': args.era, 'revision': args.revision,
              'sha256': hashlib.sha256(payload).hexdigest(), 'evidence': evidence}
    with args.output.open('xb') as output:
        output.write((json.dumps(bundle, ensure_ascii=False, indent=2, default=str) + '\n').encode())
    print(f'Exported {len(entries)} creatures and {len(evidence["quests"].get("quest_template", []))} quests; no model calls.')


if __name__ == '__main__':
    main()
