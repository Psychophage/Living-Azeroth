# Current state

Updated 2026-10-05.

## Working

- Persistent population: up to 500 online from a reserve of 2,000, about 40 per region,
  none online without a player. City bots run errands between NPCs instead of hopping.
- Character system: party, whisper, local and guild conversation; memories that only
  cost a model call when a player said something; actions through native game rules;
  Northshire knowledge catalogue; hard spending ceiling.
- Realm: Wrath content, level cap 80, double XP. Progression phases are all off, so all
  Wrath content is open. The auction house bot is installed but its seller is off.

## Open problems

- **Occasional worldserver crash** (twice so far): memory corruption in an aura on a bot
  that was just set up at login. An AddressSanitizer build has not reproduced it on the
  test realm yet; next is a long run against a copy of a large realm.
- **Auction sellers are not set up automatically.** Switching the auction house on
  needs seller characters made by hand (see [Auction house](features/auction-house.md)).

## Next

1. Auction house seller setup and phase-appropriate stock as part of the realm tools.
2. A client addon for controlling bots (planned; mockups exist).
