# Prepared character knowledge

NPCs know things about themselves, their place and the player's quests from prepared summaries.
Runtime resolves native IDs and current quest state, selects whole short records, and sends at most
`KnowledgeBytes` bytes (default 3,600; at most twelve records). It makes no lookup or summarisation
API call. Ordinary dialogue, memory processing and selection use the shared budget as before.

## The NPC summaries

`modules/mod-living-characters/knowledge/wotlk/` holds a summary for 25,530 NPCs, one file per place:

| Path | Holds |
| --- | --- |
| `eastern-kingdoms/`, `kalimdor/`, `outland/`, `northrend/` | One file per zone (`eastern-kingdoms/elwynn-forest.json`) |
| `dungeons/`, `raids/`, `battlegrounds/`, `arenas/` | One file per instance |
| `travel/` | Ships, zeppelins and the Deeprun Tram |
| `no-fixed-place/` | NPCs with no static spawn (summoned, scripted, quest creatures), by entry ID |
| `without-summary.json` | 4,417 NPCs deliberately left without one (`native only` or `excluded`), with the reason |

An NPC is filed under the zone the server reports for most of its spawns (the server's own lookup, which
sees buildings such as Ironforge, Undercity and Dalaran; the terrain under them says Dun Morogh, Tirisfal
and Crystalsong Forest). A dungeon's entrance cave shares the dungeon's file, since both are one zone. NPCs
with no spawn in the world database stay where the original research placed them.

Each place file lists its NPCs (entry, name, title and their `records`), the place's own
`place_records` (one for the zone, one per town or landmark area: what it is, who holds it, who lives
there and what troubles it) and the `sources` those records cite. NPCs standing in a place hear its
zone and area records alongside their own summary. 82 zones and 1,505 areas are described. `northshire-context.json` beside the folder adds the Northshire pilot's quest stages,
locations, report group and Common-language policy.

Look an NPC up by name or entry number, edit its `text` in the file the lookup names, then check:

```sh
cd modules/mod-living-characters
python3 tools/npc_content.py show "Marshal McBride"
python3 tools/npc_content.py show "Elwynn Forest"     # a zone's place records
python3 tools/npc_content.py check
python3 tests/npc_content_test.py
```

Set `[realm] prepared_knowledge = true` in a realm's `realm.conf`; every start builds the realm's
packs from these files (one pack per place file, plus the context file) into
`<realm>/knowledge/wotlk/` and points the server at their index. To build elsewhere:
`python3 tools/npc_content.py build --output knowledge/generated/wotlk --supplement knowledge/northshire-context.json`
(generated packs are ignored by Git; builds are byte-identical).

The NPC summaries were written and reviewed by models from the world database and recorded research;
the place records were written by Claude Haiku from evidence packets built from the world database and
map data (2026-10-15; the cities, buildings and caves the map data could not see were added on 2026-10-28
from the server's own placement of every spawn), each checked for citations, size, game terms and names not
in the evidence.
Neither has been play-tested broadly. Some are deliberately short where nothing more was supported;
`authored: true` marks invented personality or policy rather than sourced fact. The research behind
them (evidence, review decisions, the authoring tools and planning) is archived outside the
repository; source IDs and revisions stay here because the server ties reports to them.

Preview what an NPC would be told with the production resolver:

```sh
g++ -std=c++20 -O1 -Isrc/character -Isrc/llm -Ideps tools/knowledge_check.cpp src/character/pbc_knowledge.cpp -lcrypto -o /tmp/pbc-knowledge-check
/tmp/pbc-knowledge-check knowledge/generated/wotlk/index.json wotlk facts.json
```

`facts.json` can hold `{"map_id":0,"zone_id":12,"area_id":24,"entry":197,"interaction_quests":{"7":"complete"}}`.
The preview reports selected record IDs, serialized bytes, omitted records and groups. It cannot prove
quest state, model quality or in-game delivery. A pack that fails validation stops the character
system from starting; the server loads all packs or none.

## Record format

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

## Validation

Northshire passed recorded-server checks for quest accept, completion and reward, per-player quest
isolation, guild membership and shared reports, guard recall across watches, language comprehension
and local context changes. With all summaries loaded the server holds 25,554 records; the recorded
knowledge tests pass on it. Broad quest and zone knowledge (beyond NPC summaries) is not written yet:
quest overlays need relevance selection before they can scale, because many matching overlays crowd
out geography.
