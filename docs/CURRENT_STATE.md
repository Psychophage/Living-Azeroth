# Current state

Updated 2026-10-08.

## Working

- Persistent population: up to 500 online from a reserve of 2,000, about 40 per region,
  none online without a player. City bots run errands between NPCs instead of hopping.
- Character system: party, whisper, local and guild conversation; memories that only
  cost a model call when a player said something; actions through native game rules;
  Northshire knowledge catalogue; hard spending ceiling. A reply that repeats its own
  line is spoken once.
- Realm: Wrath content, level cap 80, double XP. Progression phases are all off, so all
  Wrath content is open. The auction house bot is installed but its seller is off.
- Realm tools: realms can share one spending budget (`shared_budget`); each start keeps
  the previous session's logs.
- Bot control, server side (no addon yet): an addon bridge (`mod-bot-control`, off by
  default) for orders, per-player hearing (`.chars hear`), guild identity (`.chars guild`),
  guild rumours (`.chars rumours`), "why did they say that" (`.chars why`) and the roster;
  bots can join in on their leader's auto-attack ("join attack").

## Open problems

- **Occasional worldserver crash** (twice so far): memory corruption in an aura on a bot
  that was just set up at login. An AddressSanitizer build has not reproduced it on the
  test realm yet; next is a long run against a copy of a large realm.
- **Real-model interpretation gaps** (paid evaluation, 2026-10-06): "can I have some
  water?" opens a trade without asking how many; "you don't have to stay" does not
  release a hold; a negated order's spoken reply is discarded; a compound "tank and pull"
  request did not engage.
- **A volunteer invitation is not offered after a declined duel** earlier in the same
  server session (reproducible in tests; cause not yet found).
- **A companion sometimes never appears to its player** after being added (about one
  login in 30-40 in the live tests), even after the player leaves range and returns;
  Playerbots reports the bot as logged in. Cause not yet found.
- **Misleading log error**: "World observation could not be stored" when the second step
  of a two-step order never started; nothing is lost.
- **Auction sellers are not set up automatically.** Switching the auction house on
  needs seller characters made by hand (see [Auction house](features/auction-house.md)).

## Next

1. Bot control: a guild identity draft from a few words (one model call), then the client
   addon on top of the server side (mockups done). Raid frames: own or Grid/VuhDo, undecided.
2. The unseen companion and volunteer-after-duel problems, then the interpretation gaps above.
3. Auction house seller setup and phase-appropriate stock as part of the realm tools.
4. Bots that ride boats and zeppelins: wait at the dock, board when it is in, step off at
   the other end; population uses rides for some departures and arrivals. Start by proving
   one bot survives the crossing (the continent change mid-trip) on the test realm.
