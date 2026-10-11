#!/usr/bin/env python3
"""Import preserved NPC decisions and build deterministic offline knowledge packs.

This tool does not contact a database, model, or realm. Authoring JSON is editable;
runtime JSON is generated. Importing is a one-time, verified conversion.
"""

import argparse
from collections import Counter, defaultdict
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile


AUTHORING = "living-azeroth-npc-authoring"
RUNTIME = "living-azeroth-knowledge-packs"
DISPOSITIONS = {"authored", "native_only", "excluded"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def encoded(value, pretty=False):
    options = {"indent": 2} if pretty else {"separators": (",", ":")}
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False, **options) + "\n").encode()


def value_hash(value):
    return hashlib.sha256(encoded(value)).hexdigest()


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def pointer(value, locator):
    require(not locator or locator.startswith("/"), f"Invalid JSON pointer: {locator}")
    for key in locator.split("/")[1:] if locator else []:
        key = key.replace("~1", "/").replace("~0", "~")
        value = value[int(key)] if isinstance(value, list) else value[key]
    return value


def sharded_write(root, folder, values):
    shards = defaultdict(dict)
    for key, value in values.items():
        suffix = key.removeprefix("source:")
        require(re.fullmatch(r"[0-9a-f]{2,64}", suffix), f"Invalid content key {key}")
        shards[suffix[:2]][key] = value
    for shard, value in sorted(shards.items()):
        write(root / folder / (shard + ".json"), value)


def publish(staging, output, replace=False):
    if output.exists():
        require(replace, f"Output exists: {output}; use --replace for a generated runtime directory")
        require(read(output / "index.json").get("format") == RUNTIME,
                "Refusing to replace a directory that is not generated NPC runtime content")
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


def import_final(final, backup_manifest, output):
    final = final.resolve()
    recovery = final.parents[2]
    require(not output.exists(), "Import requires a new destination; accepted work is never overwritten")
    backup = read(backup_manifest)
    require(backup.get("status") == "verified" and backup.get("every_archived_regular_file_matches_source"),
            "A verified completed-work backup is required")
    archive = Path(backup["archive"])
    require(file_hash(archive) == backup["sha256"], "Backup archive hash mismatch")
    contents_path = Path(backup["content_manifest"])
    require(file_hash(contents_path) == backup["content_manifest_sha256"], "Backup contents hash mismatch")
    contents = read(contents_path)

    @lru_cache(maxsize=32)
    def backed(path):
        path = path.resolve()
        member = str(path.relative_to(recovery.parent))
        require(member in contents, f"Not present in completed backup: {member}")
        digest = file_hash(path)
        require(digest == contents[member]["sha256"], f"Source changed since backup: {member}")
        return read(path), member, digest

    def original(name):
        return backed(final / name)[0]

    summary = original("summary.json")
    require(summary.get("goal_complete") and summary.get("unfinished_content") == 0,
            "The source final catalogue is not complete")
    coverage = original("coverage.json")
    index = original("index.json")
    classifications = {row["entry"]: row for row in original("classification-reviewed.json")["entries"]}
    inventory = backed(recovery / "restored-run/inventory-context.json")[0]
    names = {}
    for row in inventory["entries"]:
        native_names = {(v["value"]["name"], v["value"].get("subname", "")) for v in row["versions"]}
        require(len(native_names) == 1, f"Conflicting native names: {row['entry']}")
        names[row["entry"]] = next(iter(native_names))
    positions = {row["entry"]: row for row in index["entries"]}
    ids = [row["entry"] for row in coverage]
    require(len(ids) == len(set(ids)) == 29947 and set(ids) == set(positions) == set(classifications) == set(names),
            "Installed native coverage differs")
    notes = defaultdict(list)
    for row in original("unresolved-evidence.json"):
        notes[row["entry"]].append({k: v for k, v in row.items() if k != "entry"})
    sources, evidence, npcs = {}, {}, {}
    old_sources = {}
    original_records = {}
    for pack in index["packs"]:
        document = original(pack["path"])
        require(file_hash(final / pack["path"]) == pack["sha256"], "Final pack hash mismatch")
        require(not document["groups"], "NPC import must not silently discard groups")
        for record in document["records"]:
            require(record["id"] not in original_records, "Duplicate accepted record")
            original_records[record["id"]] = record
    for row in coverage:
        entry = row["entry"]
        require(row["final_content_complete"] and row["disposition"] in DISPOSITIONS,
                f"Unfinished entry: {entry}")
        draft = original(positions[entry]["logical_path"])
        require(not draft["groups"], "Unexpected entry groups")
        for source_id, metadata in draft["sources"].items():
            if source_id in sources:
                require(old_sources[source_id] == metadata, f"Conflicting source ID: {source_id}")
                continue
            old_sources[source_id] = metadata
            file, separator, locator = metadata["reference"].partition("#")
            require(separator, f"Source has no factual locator: {source_id}")
            path = Path(file)
            if not path.is_absolute():
                path = recovery.parent / path
            document, member, digest = backed(path)
            value = pointer(document, locator)
            evidence_id = value_hash(value)
            if evidence_id in evidence:
                require(evidence[evidence_id] == value, "Evidence hash collision")
            evidence[evidence_id] = value
            sources[source_id] = {
                "kind": metadata["kind"], "revision": metadata["revision"], "evidence": evidence_id,
                "origin": {"archive_path": member, "artifact_sha256": digest, "pointer": locator},
                "original_metadata_sha256": value_hash(metadata),
            }
        classification = classifications[entry]
        accepted, member, digest = backed(recovery / row["artifact"])
        require(digest == classification["artifact_sha256"], f"Accepted decision changed: {entry}")
        npcs[entry] = {
            "entry": entry, "name": names[entry][0], "title": names[entry][1],
            "region": positions[entry]["region"],
            "classification": {k: v for k, v in classification.items()
                               if k not in ("entry", "artifact", "artifact_sha256")},
            "coverage": row["original_coverage"], "records": draft["records"],
            "sources": sorted(draft["sources"]), "notes": notes[entry],
            "provenance": {"accepted_artifact": member, "accepted_artifact_sha256": digest,
                           "projection": row["projection"], "owner": row["owner"]},
        }
        require(npcs[entry]["coverage"]["disposition"] == row["disposition"], "Disposition mismatch")
    imported_records = {record["id"]: record for npc in npcs.values() for record in npc["records"]}
    require(imported_records == original_records, "Conversion changed accepted runtime records")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".npc-import-", dir=output.parent))
    try:
        shards = defaultdict(list)
        for entry, npc in sorted(npcs.items()):
            shards[entry // 1000].append(npc)
        for shard, rows in shards.items():
            write(staging / "npcs" / f"{shard * 1000:06d}-{shard * 1000 + 999:06d}.json", rows)
        sharded_write(staging, "sources", sources)
        sharded_write(staging, "evidence", evidence)
        write(staging / "manifest.json", {
            "format": AUTHORING, "version": 1, "era": index["era"], "native_ids": sorted(ids),
            "provenance_archive": {"name": archive.name, "sha256": backup["sha256"],
                                   "contents_sha256": backup["content_manifest_sha256"]},
        })
        report = {
            "source_final_manifest_sha256": file_hash(final / "completion-verification.json"),
            "source_summary_sha256": file_hash(final / "summary.json"),
            "native_entries": len(npcs), "runtime_records": len(original_records),
            "sources": len(sources), "unique_evidence_values": len(evidence),
            "accepted_records_sha256": value_hash(original_records),
            "original_source_metadata_sha256": value_hash(old_sources),
            "source_catalogue_sha256": value_hash(sources),
            "evidence_catalogue_sha256": value_hash(evidence),
            "classification_sha256": value_hash({str(e): n["classification"] for e, n in npcs.items()}),
            "coverage_sha256": value_hash({str(e): n["coverage"] for e, n in npcs.items()}),
            "notes_sha256": value_hash({str(e): n["notes"] for e, n in npcs.items()}),
            "dispositions": dict(Counter(n["coverage"]["disposition"] for n in npcs.values())),
            "text_or_decisions_rewritten": False,
            "source_reference_change": (
                "Portable evidence hash references; exact factual values and archive origins retained."
            ),
        }
        write(staging / "import.json", report)
        load_authoring(staging)
        publish(staging, output)
        return report
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def load_authoring(root):
    manifest = read(root / "manifest.json")
    require(manifest.get("format") == AUTHORING and manifest.get("version") == 1, "Unknown authoring format")
    require(manifest["era"] == "wotlk", "Unexpected authoring era")
    native_ids = manifest["native_ids"]
    require(all(type(e) is int and e > 0 for e in native_ids) and len(native_ids) == len(set(native_ids)),
            "Invalid native ID inventory")
    def mapping(folder):
        result = {}
        for path in sorted((root / folder).glob("*.json")):
            for key, value in read(path).items():
                require(key not in result, f"Duplicate {folder} key: {key}")
                require(key.removeprefix("source:")[:2] == path.stem, f"Wrong {folder} shard: {key}")
                result[key] = value
        return result
    evidence = mapping("evidence")
    for key, value in evidence.items():
        require(value_hash(value) == key, f"Evidence hash mismatch: {key}")
    sources = mapping("sources")
    for key, value in sources.items():
        require(value["evidence"] in evidence, f"Missing factual evidence: {key}")
        require(value["kind"] in ("world_db", "research", "authored"), f"Invalid source kind: {key}")
        origin = value["origin"]
        require(not Path(origin["archive_path"]).is_absolute() and ".." not in Path(origin["archive_path"]).parts,
                f"Nonportable evidence origin: {key}")
        require(re.fullmatch(r"[a-f0-9]{64}", origin["artifact_sha256"]), f"Invalid origin hash: {key}")
        require(isinstance(value["revision"], str) and 0 < len(value["revision"].encode()) <= 120,
                f"Invalid source revision: {key}")
    npcs, record_ids = {}, set()
    for path in sorted((root / "npcs").glob("*.json")):
        for npc in read(path):
            entry = npc["entry"]
            require(type(entry) is int and entry > 0 and entry not in npcs, f"Duplicate/invalid NPC entry: {entry}")
            require(path.name == f"{entry // 1000 * 1000:06d}-{entry // 1000 * 1000 + 999:06d}.json",
                    f"Wrong NPC shard: {entry}")
            npcs[entry] = npc
            coverage = npc["coverage"]
            require(coverage["entry"] == entry and coverage["disposition"] in DISPOSITIONS,
                    f"Invalid disposition: {entry}")
            require(bool(npc["records"]) == (coverage["disposition"] == "authored"),
                    f"Disposition does not match runtime records: {entry}")
            require(npc["sources"] and all(s in sources for s in npc["sources"]), f"Missing entry sources: {entry}")
            require(re.fullmatch(r"[a-z0-9-]+", npc["region"]), f"Unsafe region: {entry}")
            classification = npc["classification"]
            require(classification["baseline"]["tier"] == coverage["starting_tier"] and
                    classification["baseline"]["confidence"] == coverage["starting_confidence"],
                    f"Classification baseline mismatch: {entry}")
            require(classification["tier"] == coverage["proposed_tier"] and
                    classification["confidence"] == coverage["proposed_confidence"] and
                    classification["uncertainty"] == coverage["uncertainty"],
                    f"Accepted classification mismatch: {entry}")
            for record in npc["records"]:
                require(record["id"] not in record_ids, f"Duplicate runtime record: {record['id']}")
                record_ids.add(record["id"])
                require(record["when"].get("entry") == entry and record["layer"] == "npc",
                        f"Runtime identity mismatch: {entry}")
                require(1 <= len(record["sources"]) <= 8 and all(s in npc["sources"] for s in record["sources"]),
                        f"Record has missing citation: {entry}")
                require(isinstance(record["text"], str) and 0 < len(record["text"].encode()) <= 1200,
                        f"Invalid text length: {entry}")
                require(not any(ord(c) < 32 or ord(c) == 127 for c in record["text"]),
                        f"Control character in text: {entry}")
                require(type(record["authored"]) is bool and type(record["priority"]) is int and
                        0 <= record["priority"] <= 100, f"Invalid runtime fields: {entry}")
    require(set(npcs) == set(native_ids), "Native coverage missing or unexpected entries")
    return manifest, npcs, sources, evidence


def build(root, output, replace=False, supplements=()):
    manifest, npcs, sources, _ = load_authoring(root)
    output = output.resolve()
    require(not output.is_relative_to(root.resolve()) and not root.resolve().is_relative_to(output),
            "Generated output and authoring directories must be separate")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".npc-build-", dir=output.parent))
    packs = []
    try:
        regions = defaultdict(list)
        for entry, npc in sorted(npcs.items()):
            if npc["records"]:
                regions[npc["region"]].append(npc)
        for region, members in sorted(regions.items()):
            for offset in range(0, len(members), 500):
                records = [record for npc in members[offset:offset + 500] for record in npc["records"]]
                used = {s for record in records for s in record["sources"]}
                metadata = {s: {"kind": sources[s]["kind"], "revision": sources[s]["revision"],
                                "reference": "evidence:sha256:" + sources[s]["evidence"]} for s in sorted(used)}
                document = {"version": 1, "era": manifest["era"], "records": records,
                            "sources": metadata, "groups": []}
                relative = f"regions/{region}-part-{offset // 500 + 1:02d}.json"
                write(staging / relative, document)
                size = (staging / relative).stat().st_size
                require(len(records) <= 20000 and size <= 16 * 1024 * 1024, "Production pack limit exceeded")
                packs.append({"path": relative, "sha256": file_hash(staging / relative),
                              "records": len(records), "bytes": size})
        record_ids = {r["id"] for n in npcs.values() for r in n["records"]}
        group_ids = set()
        for number, source in enumerate(supplements):
            document = read(source)
            require(document.get("version") == 1 and document.get("era") == manifest["era"],
                    "Supplement version or era mismatch")
            for record in document["records"]:
                require(record["id"] not in record_ids, "Duplicate supplemental record")
                record_ids.add(record["id"])
                require(all(s in document["sources"] for s in record["sources"]), "Missing supplemental citation")
            for group in document["groups"]:
                require(group["id"] not in group_ids, "Duplicate supplemental group")
                group_ids.add(group["id"])
            relative = f"context/{number + 1:03d}.json"
            write(staging / relative, document)
            size = (staging / relative).stat().st_size
            require(len(document["records"]) <= 20000 and size <= 16 * 1024 * 1024, "Supplement limit exceeded")
            packs.append({"path": relative, "sha256": file_hash(staging / relative),
                          "records": len(document["records"]), "bytes": size})
        require(len(packs) <= 1024 and sum(p["bytes"] for p in packs) < 128 * 1024 * 1024,
                "Combined production pack limit exceeded")
        index = {"format": RUNTIME, "version": 1, "era": manifest["era"], "packs": packs,
                 "native_entries": len(npcs), "runtime_records": sum(p["records"] for p in packs),
                 "dispositions": dict(Counter(n["coverage"]["disposition"] for n in npcs.values()))}
        write(staging / "index.json", index)
        publish(staging, output, replace)
        return index
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def verify_import(root):
    _, npcs, sources, evidence = load_authoring(root)
    baseline = read(root / "import.json")
    records = {r["id"]: r for n in npcs.values() for r in n["records"]}
    require(value_hash(records) == baseline["accepted_records_sha256"], "Records differ from accepted import baseline")
    for field in ("classification", "coverage", "notes"):
        require(value_hash({str(e): n[field] for e, n in npcs.items()}) == baseline[field + "_sha256"],
                f"{field} differs from accepted import baseline")
    require(value_hash(sources) == baseline["source_catalogue_sha256"], "Source provenance changed since import")
    require(value_hash(evidence) == baseline["evidence_catalogue_sha256"], "Factual evidence changed since import")
    return {"accepted_records_unchanged": True, "classification_coverage_notes_unchanged": True,
            "entries": len(npcs), "records": len(records), "sources": len(sources), "evidence": len(evidence)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    migrate = commands.add_parser(
        "import-final", help="Convert a backed-up completed run into editable authoring data"
    )
    migrate.add_argument("--final", type=Path, required=True)
    migrate.add_argument("--backup-manifest", type=Path, required=True)
    migrate.add_argument("--output", type=Path, required=True)
    for name in ("check", "verify-import", "build"):
        command = commands.add_parser(name)
        command.add_argument("--source", type=Path, required=True)
        if name == "build":
            command.add_argument("--output", type=Path, required=True)
            command.add_argument("--supplement", type=Path, action="append", default=[])
            command.add_argument(
                "--replace", action="store_true", help="Replace an existing generated runtime directory"
            )
    args = parser.parse_args()
    try:
        if args.command == "import-final":
            result = import_final(args.final, args.backup_manifest, args.output)
        elif args.command == "build":
            result = build(args.source, args.output, args.replace, args.supplement)
            result = {k: v for k, v in result.items() if k != "packs"} | {"packs": len(result["packs"])}
        elif args.command == "verify-import":
            result = verify_import(args.source)
        else:
            _, npcs, sources, evidence = load_authoring(args.source)
            result = {"entries": len(npcs), "sources": len(sources), "evidence": len(evidence)}
        print(json.dumps(result, indent=2))
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(1, f"NPC content rejected: {error}\n")


if __name__ == "__main__":
    main()
