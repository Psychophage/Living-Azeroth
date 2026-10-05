# Auction house

[AHBot Plus](https://github.com/NathanHandley/mod-ah-bot-plus) (`modules/mod-ah-bot-plus`)
lists items on the auction houses and can buy from players. It is installed but off.

To switch it on, in the realm's `[auctionhouse]` section:

1. Create one or more ordinary (non-bot) characters to be the sellers; their names
   appear on listings. Set their character GUIDs in `AuctionHouseBot.GUIDs`.
2. Set `AuctionHouseBot.EnableSeller = true`, and `AuctionHouseBot.Buyer.Enabled = true`
   for the buyer.
3. `living-azeroth restart`. The houses fill over a few hours.

Every option (listing proportions, prices, buyer behaviour) is described in
`modules/mod-ah-bot-plus/conf/mod_ahbot.conf.dist`. A spreadsheet for the advanced
pricing rules is in `modules/mod-ah-bot-plus/tools/AdvancedPricingCalculator/`.

Living Azeroth additions: `AuctionHouseBot.SellerWhiteList` limits what the seller may
list (for realms that run an earlier progression phase),
`AuctionHouseBot.Buyer.AcceptablePriceModifier.TradeGood` sets a separate buyer price
for raw materials, and the `ahbot create-sellers` console command creates seller
characters on a dedicated account.

GM commands in game: `.ahbot reload`, `.ahbot update`, `.ahbot empty` (removes every
bot listing).
