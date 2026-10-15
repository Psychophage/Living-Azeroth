#!/usr/bin/env python3
"""NPC summaries: check them, look one up, and build the game's knowledge packs from them.

The summaries live in knowledge/wotlk/, one file per place (eastern-kingdoms/elwynn-forest.json, ...).
NPCs with no fixed spawn are filed by ID under no-fixed-place/; NPCs deliberately left without a
summary are listed in without-summary.json. This tool contacts no database, model or realm.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import textwrap


SUMMARIES = "living-azeroth-npc-summaries"
RUNTIME = "living-azeroth-knowledge-packs"
WITHOUT = "without-summary.json"
DECISIONS = {"native only", "excluded"}
PACK_RECORDS, PACK_BYTES = 20000, 16 * 1024 * 1024  # the server's limits per pack
TOTAL_PACKS, TOTAL_BYTES = 1024, 128 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def encoded(value, pretty=False):
    options = {"indent": 2} if pretty else {"separators": (",", ":")}
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False, **options) + "\n").encode()


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key {key!r}: {path}")
            result[key] = value
        return result
    return json.loads(path.read_bytes(), object_pairs_hook=unique)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded(value, pretty=True))


def check_record(record, entry, sources, seen, where):
    require(record["id"] not in seen, f"{where}: record {record['id']} appears twice")
    seen.add(record["id"])
    require(record["layer"] == "npc" and record["when"].get("entry") == entry,
            f"{where}: record {record['id']} is not about NPC {entry}")
    require(1 <= len(record["sources"]) <= 8 and all(s in sources for s in record["sources"]),
            f"{where}: record {record['id']} cites a source the file does not list")
    text = record["text"]
    require(isinstance(text, str) and 0 < len(text.encode()) <= 1200, f"{where}: record {record['id']} text length")
    require(not any(ord(c) < 32 or ord(c) == 127 for c in text), f"{where}: control character in {record['id']}")
    require(type(record["authored"]) is bool and type(record["priority"]) is int and 0 <= record["priority"] <= 100,
            f"{where}: record {record['id']} has invalid fields")


def load(root):
    """Every place file and the without-summary list, checked. Returns (places, without)."""
    root = Path(root)
    places, entries, records = {}, {}, set()
    for path in sorted(root.rglob("*.json")):
        relative = path.relative_to(root).as_posix()
        if relative == WITHOUT:
            continue
        document = read(path)
        require(document.get("format") == SUMMARIES and document.get("version") == 1, f"{relative}: unknown format")
        require(document.get("era") == "wotlk", f"{relative}: unexpected era")
        require(re.fullmatch(r"[a-z0-9-]+(/[a-z0-9-]+)*\.json", relative), f"{relative}: unsafe file name")
        sources = document["sources"]
        for key, source in sources.items():
            require(source["kind"] in ("world_db", "research", "authored"), f"{relative}: source {key} kind")
            require(isinstance(source["revision"], str) and 0 < len(source["revision"].encode()) <= 120,
                    f"{relative}: source {key} revision")
        cited = set()
        for npc in document["npcs"]:
            entry = npc["entry"]
            require(type(entry) is int and entry > 0, f"{relative}: invalid NPC entry {entry!r}")
            require(entry not in entries, f"{relative}: NPC {entry} is also in {entries.get(entry)}")
            entries[entry] = relative
            require(npc["records"], f"{relative}: NPC {entry} has no summary (list it in {WITHOUT} instead)")
            for record in npc["records"]:
                check_record(record, entry, sources, records, relative)
                cited.update(record["sources"])
        require(set(sources) == cited, f"{relative}: lists sources no summary cites: {sorted(set(sources) - cited)[:3]}")
        places[relative] = document
    without = read(root / WITHOUT)
    require(without.get("format") == SUMMARIES and without.get("era") == "wotlk", f"{WITHOUT}: unknown format")
    for npc in without["npcs"]:
        require(npc["entry"] not in entries, f"{WITHOUT}: NPC {npc['entry']} also has a summary in "
                                             f"{entries.get(npc['entry'])}")
        require(npc["decision"] in DECISIONS, f"{WITHOUT}: NPC {npc['entry']} decision {npc['decision']!r}")
        entries[npc["entry"]] = WITHOUT
    return places, without["npcs"]


def publish(staging, output, replace=False):
    if output.exists():
        require(replace, f"Output exists: {output}; use --replace for a generated knowledge directory")
        require(read(output / "index.json").get("format") == RUNTIME,
                "Refusing to replace a directory that is not generated knowledge")
        previous = output.with_name(output.name + ".previous")
        require(not previous.exists(), f"Previous output still exists: {previous}")
        output.rename(previous)
        try:
            staging.rename(output)
        except BaseException:
            previous.rename(output)
            raise
        shutil.rmtree(previous)
    else:
        staging.rename(output)


def build(root, output, replace=False, supplements=()):
    """One knowledge pack per place file, plus the supplements, and the index the server reads."""
    root, output = Path(root).resolve(), Path(output).resolve()
    places, without = load(root)
    require(not output.is_relative_to(root) and not root.is_relative_to(output),
            "Generated output and the summaries must be in separate folders")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".npc-build-", dir=output.parent))
    packs, record_ids, group_ids = [], set(), set()

    def add(relative, document):
        write(staging / relative, document)
        size = (staging / relative).stat().st_size
        require(len(document["records"]) <= PACK_RECORDS and size <= PACK_BYTES, f"{relative}: pack too large")
        packs.append({"path": relative, "sha256": file_hash(staging / relative),
                      "records": len(document["records"]), "bytes": size})

    try:
        for relative, place in sorted(places.items()):
            records = [record for npc in place["npcs"] for record in npc["records"]]
            record_ids.update(record["id"] for record in records)
            add(relative, {"version": 1, "era": place["era"], "records": records, "sources": place["sources"],
                           "groups": []})
        for source in supplements:
            document = read(Path(source))
            require(document.get("version") == 1 and document.get("era") == "wotlk", f"{source}: version or era")
            for record in document["records"]:
                require(record["id"] not in record_ids, f"{source}: record {record['id']} appears twice")
                record_ids.add(record["id"])
                require(all(s in document["sources"] for s in record["sources"]), f"{source}: missing citation")
            for group in document["groups"]:
                require(group["id"] not in group_ids, f"{source}: group {group['id']} appears twice")
                group_ids.add(group["id"])
            add(f"context/{Path(source).stem}.json", document)
        require(len(packs) <= TOTAL_PACKS and sum(p["bytes"] for p in packs) < TOTAL_BYTES, "Too many packs in all")
        summarised = sum(len(p["npcs"]) for p in places.values())
        # The server accepts exactly these fields in an index.
        index = {"format": RUNTIME, "version": 1, "era": "wotlk", "packs": packs,
                 "runtime_records": sum(p["records"] for p in packs), "native_entries": summarised + len(without),
                 "dispositions": {"authored": summarised,
                                  "native_only": sum(n["decision"] == "native only" for n in without),
                                  "excluded": sum(n["decision"] == "excluded" for n in without)}}
        write(staging / "index.json", index)
        publish(staging, output, replace)
        return index
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def show(root, query, out=sys.stdout):
    """Print the NPCs matching an entry number or part of a name, with their summaries."""
    places, without = load(root)
    number = int(query) if query.isdigit() else None
    needle = query.lower()
    found = 0
    for relative, place in sorted(places.items()):
        for npc in place["npcs"]:
            if npc["entry"] != number and (number is not None or needle not in npc["name"].lower()):
                continue
            found += 1
            title = f" <{npc['title']}>" if npc["title"] else ""
            out.write(f"\n{npc['name']}{title}   NPC {npc['entry']}   {place['place']}   ({relative})\n")
            for record in sorted(npc["records"], key=lambda r: -r["priority"]):
                when = {k: v for k, v in record["when"].items() if k != "entry"}
                tags = [f"priority {record['priority']}", f"{len(record['sources'])} sources"]
                if record["authored"]:
                    tags.append("authored personality")
                if when:
                    tags.append("when " + ", ".join(f"{k} {v}" for k, v in sorted(when.items())))
                out.write(textwrap.fill(record["text"], 100, initial_indent="  • ", subsequent_indent="    ") + "\n")
                out.write(f"    {' · '.join(tags)}\n")
    for npc in without:
        if npc["entry"] == number or (number is None and needle in npc["name"].lower()):
            found += 1
            out.write(f"\n{npc['name']}   NPC {npc['entry']}   no summary ({npc['decision']})\n")
            out.write(textwrap.fill(npc["reason"], 100, initial_indent="  ", subsequent_indent="  ") + "\n")
    if not found:
        out.write(f"No NPC matches {query!r}.\n")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "knowledge/wotlk",
                        help="the summaries folder (default: this module's knowledge/wotlk)")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="check every summary file")
    lookup = commands.add_parser("show", help="an NPC's summary, by entry number or part of its name")
    lookup.add_argument("query")
    builder = commands.add_parser("build", help="build the game's knowledge packs")
    builder.add_argument("--output", type=Path, required=True)
    builder.add_argument("--supplement", type=Path, action="append", default=[])
    builder.add_argument("--replace", action="store_true", help="replace an existing generated knowledge directory")
    args = parser.parse_args()
    try:
        if args.command == "build":
            index = build(args.source, args.output, args.replace, args.supplement)
            print(f"Character knowledge: {index['dispositions']['authored']:,} NPC summaries, "
                  f"{index['runtime_records']:,} records in {len(index['packs'])} packs.")
        elif args.command == "show":
            return 0 if show(args.source, args.query) else 1
        else:
            places, without = load(args.source)
            npcs = sum(len(p["npcs"]) for p in places.values())
            print(f"NPC summaries are valid: {npcs:,} NPCs in {len(places)} files, {len(without):,} without a summary.")
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(1, f"NPC summaries rejected: {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
