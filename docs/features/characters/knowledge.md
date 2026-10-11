# Prepared character knowledge

The catalogue is offline content. Runtime resolves native IDs and current quest
state, selects whole short records, and sends at most `KnowledgeBytes` bytes
(default 3,600; at most twelve records). It makes no lookup or summarisation API
call. Sources remain in the authoring file. Ordinary dialogue, memory processing
and selection continue to use the existing shared budget.

## Author and preview

Paths and commands below are relative to `modules/mod-living-characters`.

`knowledge/authoring/wotlk/` contains the completed NPC dataset. It is the editable
source; runtime packs are generated from it. Set `[realm] prepared_knowledge = true`
in the realm's `realm.conf`, then restart through `living-azeroth`. The tools build
`<realm>/knowledge/wotlk/` and configure its index automatically.

`knowledge/northshire-context.json` retains the pilot's quest stages, locations,
report group and explicit Common-language policy. Completed summaries replace its
NPC identities and invented personality instructions. Language records are marked
as authored realm policy. The original `northshire.json` is retained as a standalone
pilot example; the maintained realm configuration does not load it.

The authoring layout separates content from evidence and historical decisions:

| Path | Purpose |
| --- | --- |
| `manifest.json` | Format version, era, installed native IDs and verified archive identity |
| `npcs/000000-000999.json`, etc. | NPCs in fixed native-ID ranges: name/title, accepted records, coverage, classification, notes and provenance |
| `sources/xx.json` | Citation IDs, source revisions, evidence hashes and original archive file/hash/JSON-pointer locators |
| `evidence/xx.json` | Deduplicated factual excerpts, keyed by their SHA-256 content hashes |
| `import.json` | Frozen import baseline for proving that conversion preserved accepted work |

NPC records retain the production schema described below. Their `authored` boolean
distinguishes invented personality/policy from sourced facts; it does not mean
that a sourced summary is unfinished. Classification uncertainty stays separate
from summary notes. No-profile and excluded entries remain explicit authoring
decisions and produce no runtime records. Original packets, reviews and withdrawals
remain in the verified archive; they are not repackaged as runtime content.

To edit an NPC, find its ID in the corresponding thousand-ID file, make the
supported change, and retain or update its citation IDs. Names, classifications
and notes are authoring metadata; only `records` enter the generated catalogue.
Sources use `evidence:sha256:<hash>` references at runtime, resolved to the matching
authoring `evidence/xx.json` for inspection. The complete original artifact and
locator remain traceable through `sources/xx.json` and the archive manifest.
Adding factual evidence requires a new content hash and an explicit source origin;
existing evidence values must not be edited under an unchanged hash.

```sh
python3 tools/npc_content.py check --source knowledge/authoring/wotlk
python3 tools/npc_content.py build --source knowledge/authoring/wotlk \
  --supplement knowledge/northshire-context.json --output knowledge/generated/wotlk
python3 tests/npc_content_test.py
```

Use `--replace` to rebuild an existing generated output directory. Validation runs
before replacement; unrelated directories and the authoring tree cannot be replaced.
Generated packs are ignored by Git. Commit authoring changes and tooling; rebuild
the same runtime output from a clean checkout. Stable IDs, file ranges and sorted
serialization keep ordinary edits local and repeated builds byte-identical.

`verify-import --source knowledge/authoring/wotlk` additionally compares records,
classification, coverage, notes, source provenance and evidence with the frozen
import baseline. It is a migration check and will correctly report differences
after deliberate authoring changes. Do not regenerate the import baseline to hide
an edit; review the normal Git diff instead.

`import-final` is the one-time migration command. It requires a fresh, verified
completed-run backup and a new destination, checks source files against that backup,
and copies accepted records and decisions without reauthoring. The maintained
dataset and normal builds do not require the private recovery workspace.

Build the offline preview using the production parser/resolver:

```sh
g++ -std=c++20 -O1 -Isrc/character -Isrc/llm -Ideps tools/knowledge_check.cpp src/character/pbc_knowledge.cpp -lcrypto -o /tmp/pbc-knowledge-check
/tmp/pbc-knowledge-check knowledge/generated/wotlk/index.json wotlk facts.json
```

The optional facts file can contain:

```json
{"map_id":0,"zone_id":12,"area_id":24,"entry":197,"interaction_quests":{"7":"complete"}}
```

Preview reports selected record IDs, exact serialized bytes, omitted records and
NPC groups. It cannot prove native quest state, model quality or in-game delivery.
No services or models are contacted. Loader failure leaves the old validated
catalogue untouched; a configured invalid file prevents character-system startup.
Single-file catalogues remain supported. Pack indexes validate era, every file's
SHA-256, byte/record counts, contained relative paths, and global record/group/source
identity. Group references must resolve within their own pack. Limits are 1,024 packs,
128 MiB total, 100,000 records and 2,000 groups; each pack retains the 16 MiB and
20,000-record limit. Loading is all-or-nothing. Selection still uses native indexes
and the existing per-dialogue byte/record budget.

## Content contract for future Luna assignments

Assign a coherent location/quest cluster using exact era, native IDs, factual
evidence and accepted shared grounding. The completed run used compact author
responses expanded by the host: at most 24 KiB input, one focused follow-up of at
most 8 KiB within that total, 12 KiB expanded output and 24 records. Typical batches
contain up to twelve routine, six normal or four complex entries. Do not invent
IDs, progression rules or missing canon. Preserve recorded historical contracts
when continuing older work.

`tools/export_knowledge.py` exports creature records, linked quest text and
quest-template/addon fields, giver/ender relations and scripted speech in a read-only world
transaction. It never reads character data. Use a private connection JSON and
an explicit output path. Outputs include a source revision and evidence hash;
they are research inputs, not automatically accepted runtime content.

Each catalogue has `version: 1`, an explicit `era`, a `sources` object, `records`
and `groups` arrays. Source entries have `kind` (`world_db`, `research`, `authored`),
`reference` and `revision`. A source reference should identify the table/row or
external page and relevant section. Separate supported facts from uncertainty.

Every record requires:
- `id`: unique lowercase ASCII ID (letters, digits, colon, hyphen, underscore).
- `layer`: world, zone, location, npc, quest or group.
- `text`: one complete useful fact/brief, 1–1,200 UTF-8 bytes. Prefer 1–3 sentences.
- `authored`: whether this is authored personality/policy rather than sourced canon.
- `sources`: 1–8 source IDs, resolving within this catalogue.
- `priority`: 0–100; identity and current quest facts usually outrank scenery.
- `when`: native map/zone/area/entry IDs, required groups, and/or quest conditions.

Conditions are conjunctive. `quest_id` requires `entry` and `quest_states` chosen
from none, incomplete, complete, rewarded, failed. `none` does **not** establish
that the player is eligible to accept a quest. Quest overlays belong to the
nearby interacting player. Do not encode global changes caused by one player's
progress. If a story requires additional prerequisites or secrets that this
contract cannot express, leave it out and flag the required condition for review.

NPC records require `entry`; group records require `groups`. An unconditional
NPC record may provide `language_id` from the server's language definitions.
Do not guess a creature's language from its hostility. Existing unprofiled
creatures retain native universal monster-chat compatibility.

Avoid duplicated parent lore, exhaustive lists, unstated future events, fabricated
relationships, or private facts disguised as public geography. Several NPCs may
share a role, but a display name does not establish shared personal identity.
Use existing explicit `NpcIdentities` mappings for canonical personal continuity.

Review batches for source fidelity, era, aliases/IDs, quest-stage separation,
speech constraints and context size. Validate with the preview, then exercise
representative transitions. Import into the configured catalogue only after
review; an agent's finished assignment is not approval of its content.

## Runtime and groups

Configure `PBC.CharacterSystem.KnowledgePath`, `KnowledgeEra` and `KnowledgeBytes`.
Reload through an intentional realm restart. Existing personal histories and
owner corrections are preserved. Prepared named NPCs need no generated biography;
unresearched named NPCs receive only their supplied name/role/canon. Playerbot
biography behavior is unchanged.

Configured NPC groups have an ID, name, short description, explicit creature
entry membership with optional map/zone/area limits, report lifetime (1–720
hours), and `public_reports`. Native player guilds derive membership and chat
rights from the server; IDs `guild:<native ID>` are reserved. Officer chat is not
handled by this feature. Shared reports remain institution-owned; guild members
retain their own private memories separately.

Reports use the existing store and evidence links. The database actor kind
`watch` remains as a compatibility discriminator for **all** institutions; there
is no parallel guild history store or automatic conversion/deletion of old
watch data. Existing explicit watches remain valid memberships. Model output
uses `scope: group:<ID>` for a permitted report destination; legacy `watch`
responses remain readable. A report must cite sources witnessed by both its
writer and the receiving institution. Private channels cannot be published.

Guild chat is limited to actual members with native speak/listen rights and
ignore rules. Guild membership allows reading current shared reports; it does
not publish local/party/private speech into guild history. Leaving a guild removes
access to new reports; already learned personal memories are not erased. Group
membership or quest/location changes cancel stale in-flight dialogue.

Retrieval excludes resolved, invalidated, future-dated and expired reports. Age
starts with the oldest cited event, not the later summarisation call. Reports
about unrelated subjects stay out of context. Repeated reports citing the same
source for the same group/subject do not multiply. At most eight reports and
2,400 serialized bytes enter a reply. Raw evidence is retained for correction.

GM diagnostics: select a character and use `.chars knowledge`; `.chars group
<ID> notes/edit/resolve` inspects or corrects that selected member's institution.
`.chars watch ...` remains an alias for the selected NPC's watch. Source revisions
invalidate derived reports. These commands do not use API credit.

No autonomous recruitment, banking, inter-group rumor forwarding, reputation
penalties or global rumor simulation is implemented. Those require explicit
gameplay and information-sharing policy, not more generated lore.

## Validation and remaining scope

The Northshire pilot has passed native recorded-server checks for quest accept,
objective completion and reward; per-player quest isolation; current guild
membership and shared reports; guard recall across watches; language comprehension;
and local context changes. Real-store tests cover report deduplication, expiry,
private-source rejection and invalidation. The PBC unit run passed 92 non-paid
tests. Three GLM quest-stage calls cost $0.000973850 on the original shared $5
ledger and preserved the complete/rewarded distinction. The pilot selected about
1.1–1.4 KB of knowledge per NPC reply. This is limited validation, not a broad
lore or language-quality evaluation.

NPC authoring is complete under the recorded relaxed review standard: 25,530
summaries, 3,489 no-profile decisions and 928 exclusions cover all 29,947 native IDs.
The format conversion preserves 25,532 runtime records and the separate authoring
decisions. Some entries intentionally have very short supported descriptions;
nonblocking questions remain recorded rather than filled with invented detail.

The inventory covers 29,947 templates, 9,464 quests and 2,307 areas; many entries
need review rather than profiles. Offline production-resolver probes measured
1.2–2.2 KB for ordinary/complex examples, but 41 matching quest overlays crowded
out all geography despite fitting the byte cap. Broad quest production therefore
needs relevance selection and protected context room. The generated NPC output has
207 regional packs within the existing 20,000-record/16 MiB per-file limit. Its
compact, versioned `index.json` lists pack paths, hashes, sizes and counts.
The runtime loads these packs together, plus one Northshire context pack: 208 packs
and 25,554 records. The full source/evidence tree stays outside dialogue requests.
The October 11 conversion passed exact comparison of all accepted runtime records,
classification, coverage, notes, sources and factual excerpts. All 207 exported
packs passed the production parser; 18 representative selections matched the
original output and rejected an unrelated entry. Two full exports produced 208
byte-identical files. Seven workflow tests cover context supplements, editing, replacement, invalid
references, duplicate IDs, missing coverage and evidence tampering.
The current exporter does not include complete condition graphs, geographic
evidence, gossip, scripts or item/object starter relations; packet preparation
must supply these where relevant. NPC conversion does not complete standalone quest
or zone authoring.
See the plan for unsupported eligibility/reset/phase cases and staged validation.
The October 11 integration passed all seven recorded knowledge tests on Steven's
existing `Realms/main`: accepted NPC text reached dialogue, changing targets selected
the correct NPC, native quest accept/complete/reward and per-player isolation worked,
and language boundaries, location changes and guild/report access remained intact.
The server logged 25,554 records from 208 packs. The realm test command passed
6,028 unit tests (5,499 skipped, one disabled) and 550 population checks. All seven
offline content workflow tests also passed. No paid model calls were used for this integration.
Recorded responses prove context delivery and game-state handling, not model prose
quality across the full world. Broader quest and zone authoring remains separate.
