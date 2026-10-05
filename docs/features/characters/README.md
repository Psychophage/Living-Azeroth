# Characters

The conversation and memory system (`modules/mod-living-characters`), based on
[PBC (mod-pbc)](https://github.com/deseven/mod-pbc) by deseven. Companions and
eligible NPCs converse in the world, remember witnessed events, and respond to
natural requests. Companion gameplay runs through native Playerbots actions, with
authority, target, consent and spending checks.

It depends on the Living Azeroth versions of AzerothCore and mod-playerbots in this
repository and is not a drop-in module for other cores. Credits and licences are in
[NOTICE.md](../../../modules/mod-living-characters/NOTICE.md).

## What it does

- Playerbot biographies generated when first needed; prepared NPC identity and
  compact world/location/quest knowledge, attributed memories and owner edits.
- Party/raid, native guild chat, local speech, General and human-to-bot whispers.
  NPCs speak locally. Autonomous bot-to-bot whispers are prohibited.
- Speaker and action selection, finite exchanges, reading-time pauses, ordered
  speech/emotes and interruption when the player speaks again or leaves.
- Native movement, combat/support, supplies/trading, services/progression and group
  actions. Up to two steps; exact transfers require consent. Small character
  initiatives and occasional combat banter preserve native scripts and mechanics.
- Shared API reservations and actual-cost accounting. Unknown paid charges keep
  reservations; failed requests are not blindly retried.

Guards and native guilds share attributed, expiring institutional reports.
[Prepared knowledge](knowledge.md) describes the Northshire pilot, context
budgets, language boundaries and the offline content-authoring workflow.

The [offline field guide](guide.html) explains conversation, explicit
commands, corrections and gameplay limits. Open it in a browser.

## Configuration

Set the character system's options in the `[characters]` section of your realm's
`realm.conf`; every option is described in
`modules/mod-living-characters/conf/playerbots_characters.conf.dist`.

- It runs when the realm has a model key (`secrets/model.key`); without one it stays off.
- The key file, budget, prompts and NPC identities are set by the realm tools.
- Each realm has one spending budget, created once at setup with the ceiling from
  `budget_dollars`. Nothing raises or resets it; `living-azeroth budget` shows spending.
- `KnowledgePath`/`KnowledgeEra` select the optional prepared knowledge catalogue.
- Prompts are the module's defaults plus the realm text in `realm/prompts/`.

### The original PBC runtime

PBC's own runtime is still in the module: its generation and condensing, the
`.chars` legacy commands, the web interface (`frontend/`) and its HTTP API. It is
off by default (`PBC.Enable = 0`) and should stay off alongside this system; its
documentation is kept in [original-pbc/](original-pbc/).

## Providers

The two transports are independent. The defaults use OpenRouter for both.

| Role | Backend | Endpoint and requirements |
| --- | --- | --- |
| Dialogue/biography/memory | `openrouter` | Chat completions with strict JSON schema, provider cost reporting and configured routing price bounds |
| Selection/actions | `openrouter` | OpenRouter Decisions questions/answers, such as Jev |
| Local dialogue | `openai-local` | Self-hosted OpenAI-compatible `/v1/chat/completions` accepting the strict schema; no OpenRouter routing fields |
| Local selection | `systemone-local` | Self-hosted typed `/v1/systemone` questions/answers, such as Laya's service |

Set `ChatUrl`, `DialogueModel`, `SelectorUrl`, `SelectorModel` and routing price
limits explicitly for your chosen services. `SelectorKeyFile` is independent;
empty inherits `KeyFile` only when both transports use OpenRouter. A local
selector never receives the cloud dialogue credential through that fallback.

Local profiles explicitly assert **no provider API fee**. Calls/errors/tokens and
latency are still journalled with zero API cost. Do not select a local profile
for a billed gateway. Missing cost on an OpenRouter response still means unknown
billing and retains its hold. Self-hosting consumes your hardware/electricity.

Transport contract tests do not establish model quality. Evaluate context limits,
large option sets, grounded speech and your hardware before adopting a local
model. Laya is not bundled and is not qualified as a drop-in raid selector.

## Development and compatibility

`src/character/` owns identity/context/persistence; `src/llm/` owns validated
responses, providers and spending; `src/runtime/` owns coordination, playback,
actions and world adapters. Playerbots owns native gameplay execution. Keep
those boundaries when extending the project.

Jev interprets human requests before native feasibility checks. Uncertain or
conflicting fields can use one question comparing complete readings; unavailable
requests remain valid meanings. Dialogue receives the actor's current accepted,
pending or confirmed result. Personal proposals may abstain with `NONE`; model
output uses `after_segment=-2` for the final delivered segment or `-1` for silent
immediate action. Legacy numeric segment indices remain readable. Two proposals
wait for their required speech and execute in order; invalid sequences execute
nothing. Private bounded selector capture is optional and disabled by default.

Unit tests are in the module's `tests/` and run with `living-azeroth test`.

`modules/mod-living-characters/tools/migrate_characters.py --connection-file /private/character-connection`
imports supported legacy evidence additively, rolling back by default. Back up
first, inspect its counts, then use `--apply` if desired. It does not guess private
witnesses, overwrite owner corrections or erase old histories.
