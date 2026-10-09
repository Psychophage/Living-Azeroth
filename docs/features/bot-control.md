# Bot control

The Living Azeroth addon (`addon/LivingAzeroth`) and the server side it talks to
(`modules/mod-bot-control`). The server side is off by default: set
`BotControl.Enable = 1` in the realm's `[botcontrol]` section.

## The addon

Copy `addon/LivingAzeroth` into the game's `Interface/AddOns` folder. It works with the
game's own interface and with DragonUI; it needs neither. On a server without bot control
it says so once and stays quiet.

- **Bots at a glance**: a gold medal on a bot's party frame portrait, and "Bot · you can
  give orders" (or a grey "Bot" for someone else's) in its tooltip.
- **Orders**: a small icon beside each of your bots' party frames shows what the bot is
  doing. Click it for the orders ring; click an order and the icon pulses until the bot
  has actually done it, then shows a tick. The ring stays open for more orders; clicking
  anywhere else, Escape, or the same icon closes it, and clicking another bot's icon moves
  it there. Out of combat, the numbers 1-8 give the ring's orders.
- `/la ring`: where the ring opens (beside the frame, at the mouse, or a fixed spot), its
  size, and which orders it holds (up to eight).
- `/la unlock` and `/la lock`: drag the party frames (with DragonUI, use its `/duiedit`)
  and the ring's fixed spot.
- `/la` shows the connection; `/la debug` prints what the addon does.

The JSON code has checks that run outside the game: `lua5.1 addon/tests/json_test.lua`.

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
| `hello` | | `protocol`, `orders`, `switches`, `formations`; from now on the player is sent changes |
| `bots` | | `bots`: the player's group bots and own bots |
| `who` | `guids` (up to 40) | `bots`: those that are bots, each `guid`, `yours`, `commandable` |
| `order` | `bot`, `order`, optional `on` or `formation` | `bot` once it has acted |
| `hearing` | optional `change` | `hearing`: what the player hears from characters |
| `guild` | optional `change` | `guild_id`, `name`, `may_edit`, `identity` of the player's guild |
| `rumours` | optional `group` (administrators) or `change` | `group`, `may_change`, `rumours` |
| `why` | optional `line` | `lines` heard, or for one line `started_by`, `remembers_you`, `actions`, `cost` |
| `roster` | | `characters` (the player's others), `companions` (best known) |
| `inspect` | `bot` | `bot`, `spec`, `item_level`, `free_slots`, `money`, `zone` |
| `bring`, `dismiss` | `name` (one of the player's own characters) | Playerbots' `messages` |

Orders are `follow`, `stay`, `guard` (hold the spot where the player stands; following,
staying or falling back ends it, typed or from the addon), `attack`, `pull`, `flee` (fall
back to the player, passive), `rest` (eat and drink; refused with `reason` "not needed" at
full health and mana, or "in combat"), and `formation` with `formation` one of `near`,
`far`, `arrow`, `queue`, `circle`, `line`, `shield`, `melee`, `chaos`. They go to Playerbots
as its chat commands. The reply comes when the bot's own state shows the order (for `rest`,
when it sits down), or with `error: timeout` after `BotControl.OrderTimeoutMs` (10 seconds).

The tactic switches are orders too, with `on` true or false (without it, the switch flips):

| Switch | Playerbots strategy | Meaning |
| --- | --- | --- |
| `passive` | passive (in and out of combat) | stay out of fights |
| `loot` | loot | pick up loot after fights |
| `join` | join attack | join in when the player starts attacking |
| `aoe` | aoe | area attacks (not every class has them) |
| `behind` | behind | melee keeps out of the frontal arc |
| `threat` | threat | hold damage while the tank builds threat |
| `avoid_aoe` | avoid aoe | step out of area damage |
| `potions` | potions | use potions when low |
| `run` | flee | run when outmatched or nearly dead |
| `save_mana` | save mana | skip small heals, keep a reserve |
| `gather` | gather | herbs and ore |
| `food` | food | eat and drink after fights |
| `mount` | mount | mount when the player mounts |

A bot whose class lacks a switch's strategy leaves it out of its `switches` and refuses it
with `unsupported`.

`join` is Playerbots' "join attack": the bot treats its master's auto-attack target as an
enemy as soon as the attack starts, instead of after the first hit lands.
`AiPlayerbot.JoinLeaderAttack = 1` (in `[playerbots]`) starts every bot with it on.

A bot is described as `guid`, `name`, `class`, `level`, `role` (tank, healer,
damage), `order` (follow, stay, guard or free), `switches` (each switch its class has, on or
off), `formation`, `resting` (sitting to eat or drink), `combat`, `yours` and `commandable`.

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

Errors: `bad_request`, `too_large`, `unknown_op`, `unknown_order`, `unknown_formation`,
`unknown_bot`, `unsupported`, `not_yours`, `timeout`, `refused` (with a `reason`),
`unavailable` (character system off).
