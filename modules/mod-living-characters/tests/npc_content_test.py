"""Offline content workflow checks; no realm or model calls."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("npc_content", Path(__file__).parents[1] / "tools/npc_content.py")
content = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(content)


class ContentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "authoring"
        self.source_id = "source:" + "a" * 22
        fact = {"entry": 1, "name": "Example", "subname": "Cook"}
        evidence_id = content.value_hash(fact)
        self.sources = {self.source_id: {
            "kind": "world_db", "revision": "native-1", "evidence": evidence_id,
            "origin": {"archive_path": "snapshot/native.json", "artifact_sha256": "b" * 64, "pointer": "/0"},
        }}
        content.sharded_write(self.source, "sources", self.sources)
        content.sharded_write(self.source, "evidence", {evidence_id: fact})
        content.write(self.source / "manifest.json", {
            "format": content.AUTHORING, "version": 1, "era": "wotlk", "native_ids": [1, 2, 3],
        })
        baseline = {"tier": 2, "confidence": "low", "uncertainty": "Classification question retained."}
        self.rows = []
        for entry, disposition in [(1, "authored"), (2, "native_only"), (3, "excluded")]:
            self.rows.append({
                "entry": entry, "name": "Example", "title": "Cook", "region": "map-0-zone-12",
                "classification": {"baseline": baseline, **baseline},
                "coverage": {"entry": entry, "disposition": disposition, "reason": "Supported disposition.",
                             "starting_tier": 2, "starting_confidence": "low", "proposed_tier": 2,
                             "proposed_confidence": "low", "uncertainty": baseline["uncertainty"]},
                "sources": [self.source_id], "notes": [{"kind": "uncertainties", "items": ["Separate question."]}],
                "records": [{"id": f"npc:{entry}:identity", "layer": "npc", "text": "An example cook.",
                             "when": {"entry": entry}, "sources": [self.source_id],
                             "authored": False, "priority": 80}] if disposition == "authored" else [],
            })
        self.save_rows()

    def save_rows(self):
        content.write(self.source / "npcs/000000-000999.json", self.rows)

    def output_records(self, output):
        return {r["id"]: r for p in sorted((output / "regions").glob("*.json"))
                for r in content.read(p)["records"]}

    def test_repeated_builds_are_byte_identical_and_exclude_authoring_metadata(self):
        a, b = self.root / "first", self.root / "second"
        content.build(self.source, a)
        content.build(self.source, b)
        files = lambda p: {str(x.relative_to(p)): x.read_bytes() for x in p.rglob("*") if x.is_file()}
        self.assertEqual(files(a), files(b))
        self.assertEqual({"npc:1:identity": self.rows[0]["records"][0]}, self.output_records(a))
        index = content.read(a / "index.json")
        self.assertEqual(index["dispositions"], {"authored": 1, "native_only": 1, "excluded": 1})
        runtime = content.read(next((a / "regions").glob("*.json")))
        self.assertNotIn("classification", runtime)
        self.assertNotIn("notes", runtime)
        self.assertEqual(runtime["sources"][self.source_id]["reference"],
                         "evidence:sha256:" + self.sources[self.source_id]["evidence"])

    def test_edit_and_rebuild_preserves_other_records_and_authoring_notes(self):
        second = copy.deepcopy(self.rows[0])
        second.update(entry=4)
        second["coverage"]["entry"] = 4
        second["records"][0].update(id="npc:4:identity", when={"entry": 4})
        self.rows.append(second)
        manifest = content.read(self.source / "manifest.json")
        manifest["native_ids"].append(4)
        content.write(self.source / "manifest.json", manifest)
        self.save_rows()
        output = self.root / "runtime"
        content.build(self.source, output)
        before = self.output_records(output)
        self.rows[0]["records"][0]["text"] = "Example bears the title Cook."
        self.save_rows()
        content.build(self.source, output, replace=True)
        after = self.output_records(output)
        self.assertEqual({key for key in before if before[key] != after[key]}, {"npc:1:identity"})
        self.assertEqual(content.read(self.source / "npcs/000000-000999.json")[0]["notes"], self.rows[0]["notes"])

    def test_invalid_edit_does_not_replace_valid_output(self):
        output = self.root / "runtime"
        content.build(self.source, output)
        before = (output / "index.json").read_bytes()
        self.rows[0]["records"][0]["sources"] = ["source:missing"]
        self.save_rows()
        with self.assertRaisesRegex(ValueError, "missing citation"):
            content.build(self.source, output, replace=True)
        self.assertEqual((output / "index.json").read_bytes(), before)

    def test_duplicate_record_and_missing_native_entry_are_rejected(self):
        self.rows[0]["records"].append(copy.deepcopy(self.rows[0]["records"][0]))
        self.save_rows()
        with self.assertRaisesRegex(ValueError, "Duplicate runtime record"):
            content.load_authoring(self.source)
        self.rows[0]["records"].pop()
        self.rows.pop()
        self.save_rows()
        with self.assertRaisesRegex(ValueError, "Native coverage"):
            content.load_authoring(self.source)

    def test_evidence_tampering_and_duplicate_json_keys_are_rejected(self):
        path = next((self.source / "evidence").glob("*.json"))
        value = content.read(path)
        value[next(iter(value))]["name"] = "Unverified name"
        content.write(path, value)
        with self.assertRaisesRegex(ValueError, "Evidence hash mismatch"):
            content.load_authoring(self.source)
        path.write_bytes(b'{"same": 1, "same": 2}')
        with self.assertRaisesRegex(ValueError, "Duplicate JSON key"):
            content.read(path)

    def test_context_supplement_preserves_npc_records_and_dispositions(self):
        supplement = self.root / "context.json"
        context = {"version": 1, "era": "wotlk", "sources": {"local": {
            "reference": "quest:7", "revision": "test", "kind": "world_db"}},
            "records": [{"id": "quest:7", "layer": "quest", "text": "A task remains underway.",
                         "when": {"entry": 1, "quest_id": 7, "quest_states": ["incomplete"]},
                         "sources": ["local"], "authored": False, "priority": 90}], "groups": []}
        content.write(supplement, context)
        output = self.root / "runtime"
        index = content.build(self.source, output, supplements=[supplement])
        self.assertEqual(index["runtime_records"], 2)
        self.assertEqual(index["native_entries"], 3)
        self.assertEqual(self.output_records(output), {"npc:1:identity": self.rows[0]["records"][0]})
        path = output / index["packs"][-1]["path"]
        self.assertEqual(content.read(path), context)
        self.assertEqual(content.file_hash(path), index["packs"][-1]["sha256"])
        before = (output / "index.json").read_bytes()
        context["records"][0]["id"] = "npc:1:identity"
        content.write(supplement, context)
        with self.assertRaisesRegex(ValueError, "Duplicate supplemental record"):
            content.build(self.source, output, replace=True, supplements=[supplement])
        self.assertEqual((output / "index.json").read_bytes(), before)

    def test_replacement_refuses_an_authoring_or_unrelated_directory(self):
        with self.assertRaisesRegex(ValueError, "separate"):
            content.build(self.source, self.source / "runtime")
        target = self.root / "unrelated"
        content.write(target / "index.json", {"format": "unrelated"})
        with self.assertRaisesRegex(ValueError, "not generated NPC"):
            content.build(self.source, target, replace=True)
        self.assertEqual(content.read(target / "index.json"), {"format": "unrelated"})


if __name__ == "__main__":
    unittest.main()
