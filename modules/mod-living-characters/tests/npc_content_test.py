"""NPC summary workflow checks; no realm or model calls."""
import copy
import importlib.util
import io
from pathlib import Path
import re
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("npc_content", Path(__file__).parents[1] / "tools/npc_content.py")
content = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(content)


def place(name, npcs, sources):
    return {"format": content.SUMMARIES, "version": 1, "era": "wotlk", "place": name, "map": 0, "zone": 12,
            "npcs": npcs, "sources": sources}


class ContentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "wotlk"
        self.cite = "source:" + "a" * 24
        self.sources = {self.cite: {"kind": "research", "revision": "r1", "reference": "evidence:sha256:" + "b" * 64}}
        self.cook = {"entry": 1, "name": "Example Cook", "title": "Cook", "records": [{
            "id": "npc:1:identity", "layer": "npc", "text": "A cook in Goldshire.", "authored": False,
            "sources": [self.cite], "priority": 80, "when": {"entry": 1}}]}
        content.write(self.source / "eastern-kingdoms/elwynn-forest.json",
                      place("Elwynn Forest", [self.cook], self.sources))
        content.write(self.source / content.WITHOUT, {
            "format": content.SUMMARIES, "version": 1, "era": "wotlk",
            "npcs": [{"entry": 2, "name": "Waypoint", "title": None, "decision": "native only",
                      "reason": "A marker, not a person."}]})

    def edit(self, change):
        path = self.source / "eastern-kingdoms/elwynn-forest.json"
        document = content.read(path)
        change(document)
        content.write(path, document)

    def test_build_makes_one_pack_per_place(self):
        index = content.build(self.source, self.root / "out")
        self.assertEqual([p["path"] for p in index["packs"]], ["eastern-kingdoms/elwynn-forest.json"])
        self.assertEqual(index["native_entries"], 2)
        self.assertEqual(index["dispositions"], {"authored": 1, "native_only": 1, "excluded": 0})
        pack = content.read(self.root / "out/eastern-kingdoms/elwynn-forest.json")
        self.assertEqual(pack["records"], self.cook["records"])
        self.assertEqual(pack["sources"], self.sources)
        self.assertEqual(content.file_hash(self.root / "out" / index["packs"][0]["path"]), index["packs"][0]["sha256"])

    def test_index_has_only_fields_the_server_accepts(self):
        loader = (Path(__file__).parents[1] / "src/character/pbc_knowledge.cpp").read_text()
        allowed = re.search(r'Keys\(root, \{([^}]*"packs"[^}]*)\}\)', loader).group(1)
        index = content.build(self.source, self.root / "out")
        self.assertLessEqual(set(index), set(re.findall(r'"([a-z_]+)"', allowed)))

    def test_builds_are_identical(self):
        first = content.build(self.source, self.root / "one")
        second = content.build(self.source, self.root / "two")
        self.assertEqual(first, second)

    def test_edited_summary_reaches_the_pack(self):
        self.edit(lambda d: d["npcs"][0]["records"][0].update(text="A cook who burns everything."))
        content.build(self.source, self.root / "out")
        pack = content.read(self.root / "out/eastern-kingdoms/elwynn-forest.json")
        self.assertEqual(pack["records"][0]["text"], "A cook who burns everything.")

    def test_replace_only_generated_output(self):
        content.build(self.source, self.root / "out")
        with self.assertRaisesRegex(ValueError, "use --replace"):
            content.build(self.source, self.root / "out")
        content.build(self.source, self.root / "out", replace=True)
        (self.root / "other").mkdir()
        content.write(self.root / "other/index.json", {"format": "something else"})
        with self.assertRaisesRegex(ValueError, "not generated knowledge"):
            content.build(self.source, self.root / "other", replace=True)
        with self.assertRaisesRegex(ValueError, "separate folders"):
            content.build(self.source, self.source / "packs")

    def test_rejects_a_missing_citation(self):
        self.edit(lambda d: d["npcs"][0]["records"][0].update(sources=["source:" + "c" * 24]))
        with self.assertRaisesRegex(ValueError, "cites a source the file does not list"):
            content.load(self.source)

    def test_rejects_an_npc_in_two_places(self):
        content.write(self.source / "kalimdor/durotar.json", place("Durotar", [copy.deepcopy(self.cook)], self.sources))
        with self.assertRaisesRegex(ValueError, "NPC 1 is also in"):
            content.load(self.source)

    def test_rejects_a_summary_for_an_npc_listed_without_one(self):
        other = copy.deepcopy(self.cook)
        other.update(entry=2)
        other["records"][0].update(id="npc:2:identity", when={"entry": 2})
        self.edit(lambda d: d["npcs"].append(other))
        with self.assertRaisesRegex(ValueError, "also has a summary"):
            content.load(self.source)

    def test_rejects_uncited_sources(self):
        self.edit(lambda d: d["sources"].update({"source:" + "d" * 24: dict(self.sources[self.cite])}))
        with self.assertRaisesRegex(ValueError, "lists sources no summary cites"):
            content.load(self.source)

    def test_supplement_becomes_a_context_pack(self):
        supplement = self.root / "northshire-context.json"
        content.write(supplement, {"version": 1, "era": "wotlk", "groups": [], "sources": self.sources, "records": [{
            "id": "zone:12:example", "layer": "zone", "text": "Elwynn is green.", "authored": False,
            "sources": [self.cite], "priority": 50, "when": {"zone_id": 12}}]})
        index = content.build(self.source, self.root / "out", supplements=[supplement])
        self.assertIn("context/northshire-context.json", [p["path"] for p in index["packs"]])
        self.assertEqual(index["runtime_records"], 2)
        content.write(supplement, {**content.read(supplement), "records": copy.deepcopy(self.cook["records"])})
        with self.assertRaisesRegex(ValueError, "appears twice"):
            content.build(self.source, self.root / "again", supplements=[supplement])

    def test_show_finds_by_name_and_number(self):
        out = io.StringIO()
        self.assertEqual(content.show(self.source, "example", out), 1)
        self.assertIn("A cook in Goldshire.", out.getvalue())
        self.assertIn("eastern-kingdoms/elwynn-forest.json", out.getvalue())
        out = io.StringIO()
        self.assertEqual(content.show(self.source, "2", out), 1)
        self.assertIn("no summary (native only)", out.getvalue())
        self.assertEqual(content.show(self.source, "nobody", io.StringIO()), 0)


if __name__ == "__main__":
    unittest.main()
