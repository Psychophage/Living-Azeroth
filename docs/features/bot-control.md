# Bot control

The server side of the bot control addon (`modules/mod-bot-control`). The addon itself
has not shipped yet; this page describes what the server offers it. Off by default:
set `BotControl.Enable = 1` in the realm's `[botcontrol]` section.

## Who may do what

An order is allowed exactly when Playerbots would allow the same order by whisper: a
player commands their own bots and bots whose master they are; game masters command
any bot. Anyone with the addon may ask which nearby characters are bots. The server
checks every request; the addon only hides what a player cannot use.

## Messages

The addon whispers its own player in the addon language with the prefix `LABC`; the
server answers the same way, and these whispers never reach the chat frame of anyone
else. Each message is JSON, split into frames of at most 250 bytes:
`1<id>:<part>/<total>:<chunk>` (protocol 1, an id of 1-6 letters or digits, parts
counted from 1, at most 24 parts or 4 KB per message).

Requests carry an `id` and an `op`; each gets one reply with `"re"` set to that id and
`"ok"` true, or false with an `error`.

| `op` | Fields | Reply |
| --- | --- | --- |
| `hello` | | `protocol`, `orders`; from now on the player is sent changes |
| `bots` | | `bots`: the player's group bots and own bots |
| `who` | `guids` (up to 40) | `bots`: those that are bots, each `guid`, `yours`, `commandable` |
| `order` | `bot`, `order`, optional `on` | `bot` once it has acted |
| `hearing` | optional `change` | `hearing`: what the player hears from characters |
| `guild` | optional `change` | `guild_id`, `name`, `may_edit`, `identity` of the player's guild |
| `rumours` | optional `group` (administrators) or `change` | `group`, `may_change`, `rumours` |
| `why` | optional `line` | `lines` heard, or for one line `started_by`, `remembers_you`, `actions`, `cost` |
| `roster` | | `characters` (the player's others), `companions` (best known) |
| `inspect` | `bot` | `bot`, `spec`, `item_level`, `free_slots`, `money`, `zone` |
| `bring`, `dismiss` | `name` (one of the player's own characters) | Playerbots' `messages` |

Orders are `follow`, `stay`, `attack`, `pull`, `flee`, and the switches `passive`, `loot`
and `join` (`on` true or false; without it, the switch flips). They go to Playerbots as
its chat commands. The reply comes when the bot's own state shows the order, or with
`error: timeout` after `BotControl.OrderTimeoutMs` (10 seconds).

`join` is Playerbots' "join attack": the bot treats its master's auto-attack target as an
enemy as soon as the attack starts, instead of after the first hit lands.
`AiPlayerbot.JoinLeaderAttack = 1` (in `[playerbots]`) starts every bot with it on.

A bot is described as `guid`, `name`, `class`, `level`, `role` (tank, healer,
damage), `order` (follow, stay or free), `passive`, `loot`, `join`, `combat`, `yours` and
`commandable`.

After `hello`, the server sends `{"ev":"bot","bot":{...}}` whenever one of the
player's bots changes, however it changed, and `{"ev":"gone","guid":N}` when one
leaves; it checks once a second.

`hearing` is the player's own choice of what characters say to them, the same settings
as `.chars hear` (see [Characters](characters/README.md#what-you-hear)): `party`,
`nearby`, `guild`, `general` each `chatty`, `spoken` (only replies to the player) or
`silent`; `remarks` (`often`, `sometimes`, `rarely`, `never`); `banter` (`off`,
`sometimes`, `often`); `reading` (words a minute, 0 for the realm's) and `turns` (longest
exchange, 0 for the realm's). `change` carries only the fields to change.

`guild` is the guild's own identity, as `.chars guild` (see
[Characters](characters/README.md#guild-identity)): `purpose`, `values`, `traditions`,
`ambitions`, `voice` (each up to 400 characters) and `report_hours` (how long its rumours
last, 1-720). Only ranks that may set the guild's message of the day may change it.
`{"op":"guild","draft":"a few words"}` asks for a draft of the five fields (one model call
from the dialogue budget); the reply's `draft` is for the officer to edit and then send as
a `change`; nothing is saved by drafting.

`rumours` lists the guild's reports, resolved ones included: `id`, `version`, `text`,
`by` (who witnessed it), `zone`, `age_ms`, `fades_in_ms`, `resolved`, `corrected`. A
`change` is `{"note","version","action"}` with `action` `correct` (and `text`), `resolve`
or `forget`, for officers; it replies once stored. Administrators may name any `group`,
such as a town watch.

`why` without a `line` lists the last twenty lines characters said to the player (`line`,
`speaker`, `channel`, `time_ms`, `text`). With one, it explains that line, only if it was
delivered to the player: what started the conversation, what the speaker currently
remembers about the player (not only what that line drew on, which is not recorded), the
native actions it asked for, and what its model requests cost (`cost`, `cost_dollars`).

`roster` gives each other character on the player's account (`in_world`, `in_party`) and
up to twelve characters the player has come to know (population familiarity, best first).
`inspect` works for bots in the player's group or under their command. `bring` adds one of
the player's own characters as their bot, which joins their party, and `dismiss` sends it
home, through Playerbots' `.playerbots bot add/remove`.

Errors: `bad_request`, `too_large`, `unknown_op`, `unknown_order`, `unknown_bot`,
`not_yours`, `timeout`, `refused` (with a `reason`), `unavailable` (character system off).
