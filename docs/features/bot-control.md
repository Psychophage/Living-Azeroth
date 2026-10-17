# Bot control

The Living Azeroth addon (`addon/LivingAzeroth`) and the server side it talks to
(`modules/mod-bot-control`). The server side is off by default: set
`BotControl.Enable = 1` in the realm's `[botcontrol]` section.

## The addon

Copy `addon/LivingAzeroth` into the game's `Interface/AddOns` folder. It works with the
game's own interface and with DragonUI; it needs neither. On a server without bot control
it says so once and stays quiet. Its windows are its own design, drawn from plain textures
on whole screen pixels (`Draw.lua`): each window is scaled so one unit is a whole number of
pixels (two on a 1440 screen), and frames, buttons and corners are built from one-pixel
strips and a corner atlas shown 1:1, so edges stay sharp at any UI scale. The look (colours,
fonts, every surface's recipe) is in `Style.lua`, in bronze or, with DragonUI on, graphite.
Icons, the ring's disc and parts, and the corner atlas come from `addon/tools/make_art.py`.

A round button on the minimap's edge opens the manager (right-click: the settings); drag
it around the minimap. It sits as far out as other addons' minimap buttons.

- **Bots at a glance**: "Bot · you can give orders" (or a grey "Bot" for someone else's)
  in a bot's tooltip, and a round mark on its party frame portrait: grey for someone
  else's bot.
- **Orders**: on your own bots the portrait mark shows what the bot is doing. Click it for
  the orders ring: the orders are the parts of a ring around a disc that names the bot and
  what it is doing, the current order lit in its colour. Click an order and the mark
  pulses until the bot has actually done it, then shows a tick. The ring stays open for
  more orders; clicking its middle, anywhere else, Escape, or the same mark closes it, and
  clicking another bot's mark moves it there. Out of combat, the numbers 1-8 give the
  ring's orders. Its layout is a setting: *classic* (every order a part, in your order),
  *by kind* (where the bots are, then things to do, then switches, each switch with a
  light that shows whether it is on) or *two rings* (the bots' state inside, things to do
  outside). So is how Follow, Stay and Guard show: as parts of their own, as one part that
  toggles Follow and Stay, or as one that steps through all three (right-click steps back).
  A combined part shows where the bots are, with a light for each place; pointing at it
  names what a click does. With two or more of your bots in the party, or any in a raid, the "All my
  bots" button opens the ring for all of them; it stands on its own wherever you put it.
- **Raid frames** (in a raid): compact cells by group with class-coloured health, a thin
  power bar and the game's round role icons; clicking a cell targets that player,
  right-click gives the usual menu. The bars are drawn at the cell's own size, nothing
  stretched; their "gradient" look is that of DragonUI's party bars. Your bots' cells carry
  the order mark (on every bot, or the bot mark on those simply following you), with a moon
  when a bot stays out of fights (on the party frames, the mark's tooltip says so). A
  group's name opens the ring for that group's bots, and shift-clicking order marks picks a
  selection ("Orders for N selected" and "Clear" appear beside "All my bots"). Someone
  else's bots show a small grey bot mark. With the CompactRaidFrame addon installed, its frames are hidden while these
  are in use (a setting keeps them).
- **The manager window** (`/la` or the minimap button), with tabs along the bottom. Each
  tab has a list on the left (folding headers, a scroll bar) and the chosen entry on the
  parchment on the right, with its buttons under the list:
  - *Roster*: your other characters (bring one in as a bot, which joins your group, or send
    it home) and the characters you know best (invite the ones online). The chosen one's
    portrait, and for a bot in your group its talents, item level, free bag slots, money
    and what it is doing; for anyone, what they remember of you.
  - *Tactics*: "Apply to" all your bots or one of them. Tactic sets (Questing, Dungeon,
    Raid, Grinding, Quietly, Guard me, Night watch, and your own: "Save set"; right-click
    your own in the list to forget it) set many switches at once, and the set
    the bots match is named with what it does. Each box shows the bots' real state, a
    dash when they differ, greyed out when their class lacks it. "Undo" puts back what
    they had. "Formation" opens the formation window: pick one and the preview's bots glide
    to where they would stand by role; "Use" it to move them.
  - *Dialogue*: "What you hear" (who talks to you on each channel, remarks, combat banter,
    reading speed and the longest exchange; your own settings, kept by the server) and
    "Why did they say that?" for the lines said to you. Administrators also see the
    dialogue budget.
  - *Guild*: your guild's name, members and bots online, and a button for the game's own
    guild window. "Who we are" is the guild's identity to read; officers press Edit to
    change it, or start from a few words ("Write a draft" costs one model call from the
    dialogue budget and saves nothing until you press Save). "Rumours" lists what members
    have seen, with who, where and when it fades; officers correct, resolve or forget them,
    and set how long rumours last.
- The settings (`/la settings` or right-click the minimap button), in four tabs (the last, Trading, is
  below). *Orders ring*: where it opens (beside the frame, at the mouse, or a fixed spot),
  its size (70% to 130%), its layout and how the movement orders show (above), and which
  orders it holds (up to eight); the ring itself shows beside the window as it will look.
  *Raid frames*, in three parts beside a sample group drawn as it will look: *Layout*
  (groups side by side or in rows; power bars, group names, order marks on every bot, and
  hiding the CompactRaidFrame addon's frames), *Look* (bars gradient, smooth, glossy or
  flat; health in class colours, darker, or green; names white or in class colours; role
  icons for tanks and healers, everyone, or no one) and *Sizes* (the cells' width and
  height, the space between cells and between groups, the power bar's height, and the size
  of names, health text, group names, role icons and order marks). *All my bots*: whether
  the button shows, its look (medal and name, medal, or name) and its size, drawn as it will
  look. "Move frames" (or `/la unlock`
  and `/la lock`) outlines the party frames (with DragonUI, use its `/duiedit`), the raid
  groups in use and "All my bots": press inside an outline and drag, and the frame moves
  with the mouse straight away, visible where it lands. With "a fixed spot" the ring
  itself shows at its size to drag into place. Out of combat only.
- **Trading with bots** (with `AiPlayerbot.TradeShares.Enabled` on the server): opening a
  trade with a bot opens its bags beside the trade window, in the style of the Bagnon bag
  addon (`/la bags` opens them for a targeted bot). Click an item, or drag it onto the trade
  window, and the bot puts it in; click one of its items in the trade window to take it back
  out; "Give" sets its gold. Your own characters give everything. Other bots give back,
  free, what they got while grouped with you (loot and what you gave them, ticked) and gold
  they gained then, at most what they still have; random bots sell the rest at a price
  (a coin), and someone else's own characters keep theirs. With a random bot's items in the
  trade the window says what it asks, and "Pay" puts that gold on your side. What you pay
  is the bot's; gold you give beyond its price is a gift, shared back like a given item.
  The server decides all of this when the trade is accepted; the window only shows it. Bots no longer whisper their item
  lists when a trade opens. In the settings' Trading tab, game masters can switch on trading
  freely with any bot.
- `/la status` shows the connection; `/la debug` prints what the addon does.

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
| `hello` | | `protocol`, `orders`, `switches`, `formations`, `trade` (`shares`, `may_trade_freely`, `trading_freely`); from now on the player is sent changes |
| `bots` | | `bots`: the player's group bots and own bots |
| `who` | `guids` (up to 40) | `bots`: those that are bots, each `guid`, `yours`, `commandable` |
| `order` | `bot`, `order`, optional `on` or `formation` | `bot` once it has acted |
| `hearing` | optional `change` | `hearing`: what the player hears from characters |
| `guild` | optional `change` | `guild_id`, `name`, `may_edit`, `identity`, `members`, `bots_online` of the player's guild |
| `rumours` | optional `group` (administrators) or `change` | `group`, `may_change`, `rumours` |
| `why` | optional `line` | `lines` heard, or for one line `started_by`, `remembers_you`, `actions`, `cost` |
| `memory` | `character` (a low guid) | `remembers_you`: that character's notes about the player (nothing about anyone else) |
| `budget` | | `spent_dollars`, `held_dollars`, `ceiling_dollars` of the dialogue budget (administrators only; read-only) |
| `roster` | | `characters` (the player's others), `companions` (best known) |
| `inspect` | `bot` | `bot`, `spec`, `item_level`, `free_slots`, `money`, `zone` |
| `bring`, `dismiss` | `name` (one of the player's own characters) | Playerbots' `messages` |
| `bags` | `bot` (in the player's group, commanded, or trading with them) | `name`, `money`, `allowance` (gold it gives), `full`, `sells`, `shared` (entry → count free), `stacks` (each `[bag, slot, entry, count, property, price, flags]`, flags 1 tradable, 2 in the trade), `trading`, `asking` (while trading: the copper a random bot wants on the player's side before it accepts) |
| `offer` | `bot`, `bag`, `slot` | the bot puts that stack (or its shared part) in the open trade; refused with a `reason` |
| `offer_gold` | `bot`, `copper` | the bot sets its gold in the trade, at most its `allowance` |
| `take_back` | `bot`, `slot` (trade slot from 0) | the bot takes that item back out of the trade |
| `trade_freely` | `on` | `trading_freely`; game masters only (`AiPlayerbot.TradeShares.GmLevel`) |

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
| `buffs` | buff | keep their own and the group's buffs up (classes that buff) |

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
