# Prepared character knowledge

The catalogue is offline content. Runtime resolves native IDs and current quest
state, selects whole short records, and sends at most `KnowledgeBytes` bytes
(default 3,600; at most twelve records). It makes no lookup or summarisation API
call. Sources remain in the authoring file. Ordinary dialogue, memory processing
and selection continue to use the existing shared budget.

## Author and preview

`knowledge/northshire.json` is a small WotLK example, grounded in the installed
quest/creature database and AreaTable DBC. Personality and organisation policy
are explicitly authored. It is a pilot, not a researched world catalogue.

Build the offline preview using the production parser/resolver:

```sh
g++ -std=c++20 -O1 -Isrc/character -Isrc/llm -Ideps tools/knowledge_check.cpp src/character/pbc_knowledge.cpp -o /tmp/pbc-knowledge-check
/tmp/pbc-knowledge-check knowledge/northshire.json wotlk facts.json
```

The optional facts file can contain:

```json
{"map_id":0,"zone_id":12,"area_id":24,"entry":197,"interaction_quests":{"7":"complete"}}
```

Preview reports selected record IDs, exact serialized bytes, omitted records and
NPC groups. It cannot prove native quest state, model quality or in-game delivery.
No services or models are contacted. Loader failure leaves the old validated
catalogue untouched; a configured invalid file prevents runtime startup.

## Content contract for future Luna assignments

Assign one coherent location/quest cluster. Target 4–12 authored NPCs per packet;
up to 32 entries is appropriate only for a bounded review with measured output. Supply the
exact era, native IDs, exported evidence, existing parent records and this
contract. Do not ask agents to invent IDs, progression rules or missing canon.
No bulk agents have been launched by this implementation.

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

## Validation and next step

The Northshire pilot has passed native recorded-server checks for quest accept,
objective completion and reward; per-player quest isolation; current guild
membership and shared reports; guard recall across watches; language comprehension;
and local context changes. Real-store tests cover report deduplication, expiry,
private-source rejection and invalidation. The PBC unit run passed 92 non-paid
tests. Three GLM quest-stage calls cost $0.000973850 on the original shared $5
ledger and preserved the complete/rewarded distinction. The pilot selected about
1.1–1.4 KB of knowledge per NPC reply. This is limited validation, not a broad
lore or language-quality evaluation.

The next step is a bounded Alliance/Horde authoring comparison: prepared evidence
packets per region, unique NPC and quest ownership, and fixed source and context budgets.

The inventory covers 29,947 templates, 9,464 quests and 2,307 areas; many entries
need review rather than profiles. Offline production-resolver probes measured
1.2–2.2 KB for ordinary/complex examples, but 41 matching quest overlays crowded
out all geography despite fitting the byte cap. Broad quest production therefore
needs relevance selection and protected context room; full-world import also
needs catalogue scaling beyond the current 20,000-record/16 MiB single-file limit.
The current exporter does not include complete condition graphs, geographic
evidence, gossip, scripts or item/object starter relations; packet preparation
must supply these where relevant. No runtime changes were made by this planning.
See the plan for unsupported eligibility/reset/phase cases and staged validation.
No bulk content agents have been started. The feature was exercised on a separate
recorded realm; it has not been enabled on the human playtest.

The integration patches include the population branch. The combined build passes
the 92 PBC unit tests, the four knowledge tests and population arrival. It has
not been deployed to the human playtest.
