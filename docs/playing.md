# Playing

The realm runs Wrath of the Lich King content with a level cap of 80 and double XP.
Bot characters share the world with you; they arrive gradually and gather where
players are, so a zone fills up a little after you arrive.

## Companions

Invite any friendly bot you meet: right-click its portrait and choose **Invite**, or
`/invite Name`. It may decline if it is busy. Talk to your party naturally in party
chat (`/p How are we doing?`) or whisper one character (`/w Name What do you think?`).
They remember what happened and who you are. Characters also speak up on their own now
and then, and react briefly in combat; this is deliberately occasional.

Orders start with `@` and go straight to Playerbots; they never become conversation.
Without the `@`, a request like "follow me" is understood as conversation and acted on
through the character system, which uses the language model.

| What you want | Type |
| --- | --- |
| Whole party follows you | `/p @follow` |
| One bot follows you (also cancels `stay`) | `/w Name @follow` |
| Hold position | `/p @stay` or `/w Name @stay` |
| Attack your selected target | `/p @attack` |
| A bot's command list | `/w Name @help` |

## Loot, gear and selling

- With **Group Loot**, bots loot and share drops as normal. Switch to **Free for All**
  as party leader if you want to loot every corpse yourself; bots then stop looting.
- `/w Name @inv` lists a bot's bags; `/w Name @e [item link]` equips an item it holds;
  `/w Name @s *` sells its grey items at a vendor.
- Roaming bots manage their own gear. For a companion you equip and level on purpose,
  use one of your own characters as a bot (below).

## Your own characters as bots

Create another character on your account, log in with your main, then:

| What you want | Type |
| --- | --- |
| Bring an alt into the world as a bot | `.playerbots bot add AltName`, then `/invite AltName` |
| List or remove your bots | `.playerbots bot list` / `.playerbots bot remove AltName` |
| Train at a trainer (bot nearby, with money) | `/w AltName @trainer learn` |
| Talents | `/w AltName @talents`, `@talents spec list`, `@talents spec Name` |

Avoid `@maintenance` and `@autogear` if you want natural progression: they grant free
spells, gear, reputation and attunements.

## Administrator commands

The account created at setup is an administrator. Play with `.gm off`.

| Command | Use |
| --- | --- |
| `.gm on` / `.gm off` | toggle GM mode |
| `.gps` | position of you or your target; useful in bug reports |
| `.chars help` | the character system's commands |
| `.chars info Name`, `.chars history Name`, `.chars notes Name` | inspect a character, its recent history and its notes |

Avoid `.playerbots rndbot init` during normal play: it regenerates bots.

## If something seems wrong

- **No bots yet:** the population arrives over a few minutes after the server is ready.
- **A bot obeys but does not talk:** `@` orders are not conversation. Use a normal sentence.
- **A companion disappears after leaving your party:** bots come and go. Characters you
  spend time with are remembered and come back more often.
- More detail: [Playerbots command guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Playerbot-Commands)
  and the character system's [field guide](features/characters/guide.html).
