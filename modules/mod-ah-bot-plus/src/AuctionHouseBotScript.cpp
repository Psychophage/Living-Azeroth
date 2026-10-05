/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
*/

#include "Chat.h"
#include "ScriptMgr.h"
#include "AuctionHouseBot.h"
#include "Log.h"
#include "Mail.h"
#include "Player.h"
#include "WorldSession.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "ObjectMgr.h"
#include "World.h"
#include <iterator>
#include <set>
#include <sstream>

class AHBot_WorldScript : public WorldScript
{
private:
    bool HasPerformedStartup;

public:
    AHBot_WorldScript() : WorldScript("AHBot_WorldScript"), HasPerformedStartup(false) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        if (!auctionbot->IsModuleEnabled())
            return;

        auctionbot->InitializeConfiguration();
        if (HasPerformedStartup == true)
        {
            LOG_INFO("server.loading", "AuctionHouseBot: (Re)populating item candidate lists ...");
            auctionbot->PopulateItemCandidatesAndProportions();

            if (sConfigMgr->GetOption<bool>("AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled", false))
            {
                auctionbot->PopulateQuestRewardItemIDs();
                auctionbot->PopulateItemDropChances();
            }
        }
    }

    void OnStartup() override
    {
        if (!auctionbot->IsModuleEnabled())
            return;

        LOG_INFO("server.loading", "AuctionHouseBot: (Re)populating item candidate lists ...");
        auctionbot->PopulateItemCandidatesAndProportions();
        if (sConfigMgr->GetOption<bool>("AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled", false))
        {
            auctionbot->PopulateQuestRewardItemIDs();
            auctionbot->PopulateItemDropChances();
        }
        HasPerformedStartup = true;
    }
};

class AHBot_AuctionHouseScript : public AuctionHouseScript
{
public:
    AHBot_AuctionHouseScript() : AuctionHouseScript("AHBot_AuctionHouseScript") { }

    void OnBeforeAuctionHouseMgrSendAuctionSuccessfulMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* /*auction*/, Player* owner, uint32& /*owner_accId*/, uint32& /*profit*/, bool& sendNotification, bool& updateAchievementCriteria, bool& /*sendMail*/) override
    {
        if (owner)
        {
            bool isAHBot = false;
            for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
            {
                if (character.CharacterGUID == owner->GetGUID().GetCounter())
                {
                    isAHBot = true;
                    break;
                }
            }
            if (isAHBot == true)
            {
                sendNotification = false;
                updateAchievementCriteria = false;
            }
        }
    }

    void OnBeforeAuctionHouseMgrSendAuctionExpiredMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* /*auction*/, Player* owner, uint32& /*owner_accId*/, bool& sendNotification, bool& sendMail) override
    {
        if (owner)
        {
            bool isAHBot = false;
            for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
            {
                if (character.CharacterGUID == owner->GetGUID().GetCounter())
                {
                    isAHBot = true;
                    break;
                }
            }
            if (isAHBot == true)
            {
                sendNotification = false;

                if (sConfigMgr->GetOption<bool>("AuctionHouseBot.ReturnExpiredAuctionItemsToBot", false))
                    sendMail = true;
                else
                    sendMail = false;
            }
        }   
    }

    void OnBeforeAuctionHouseMgrSendAuctionOutbiddedMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* auction, Player* oldBidder, uint32& /*oldBidder_accId*/, Player* newBidder, uint32& newPrice, bool& /*sendNotification*/, bool& /*sendMail*/) override
    {
        if (oldBidder && !newBidder)
            oldBidder->GetSession()->SendAuctionBidderNotification((uint32)auction->GetHouseId(), auction->Id, ObjectGuid::Create<HighGuid::Player>(auctionbot->CurrentBotCharGUID), newPrice, auction->GetAuctionOutBid(), auction->item_template);
    }

    void OnBeforeAuctionHouseMgrSendAuctionWonMail(AuctionHouseMgr* /*auctionHouseMgr*/, AuctionEntry* /*auction*/, Player* bidder, uint32& /*bidder_accId*/, bool& sendNotification, bool& updateAchievementCriteria, bool& /*sendMail*/) override
    {
        // The bot buyer is a shell Player that never went through a full load (no map, no achievement data),
        // so suppress the paths that would touch that missing state when it wins an auction
        if (bidder)
        {
            bool isAHBot = false;
            for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
            {
                if (character.CharacterGUID == bidder->GetGUID().GetCounter())
                {
                    isAHBot = true;
                    break;
                }
            }
            if (isAHBot == true)
            {
                sendNotification = false;
                updateAchievementCriteria = false;
            }
        }
    }

    void OnBeforeAuctionHouseMgrUpdate() override
    {
        auctionbot->Update();
    }
};

class AHBot_MailScript : public MailScript
{
public:
    AHBot_MailScript() : MailScript("AHBot_MailScript") { }

    void OnBeforeMailDraftSendMailTo(MailDraft* /*mailDraft*/, MailReceiver const& receiver, MailSender const& sender, MailCheckMask& /*checked*/, uint32& /*deliver_delay*/, uint32& /*custom_expiration*/, bool& deleteMailItemsFromDB, bool& sendMail) override
    {
        bool isAHBot = false;
        for (AuctionHouseBotCharacter character : auctionbot->AHCharacters)
        {
            if (character.CharacterGUID == receiver.GetPlayerGUIDLow())
            {
                isAHBot = true;
                break;
            }
        }
        if (isAHBot == true)
        {
            if (sConfigMgr->GetOption<bool>("AuctionHouseBot.ReturnExpiredAuctionItemsToBot", false))
            {
                deleteMailItemsFromDB = false;
                sendMail = true;
            }
            else
            {
                if (sender.GetMailMessageType() == MAIL_AUCTION)        // auction mail with items
                    deleteMailItemsFromDB = true;
                sendMail = false;
            }
        }
    }
};

class AHBot_CommandScript : public CommandScript
{
public:
    AHBot_CommandScript() : CommandScript("AHBot_CommandScript") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable AHBotCommandTable = {
            {"update", HandleAHBotUpdateCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"reload", HandleAHBotReloadCommand, SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"empty",  HandleAHBotEmptyCommand,  SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes},
            {"create-sellers", HandleCreateSellersCommand, SEC_CONSOLE, Acore::ChatCommands::Console::Yes},
            {"help",  HandleAHBotHelpCommand,  SEC_GAMEMASTER, Acore::ChatCommands::Console::Yes}
        };

        static Acore::ChatCommands::ChatCommandTable commandTable = {
            {"ahbot", AHBotCommandTable},
        };

        return commandTable;
    }

    static bool HandleAHBotUpdateCommand(ChatHandler* handler, const char* /*args*/)
    {
        LOG_INFO("module", "AuctionHouseBot: Updating Auction House...");
        handler->PSendSysMessage("AuctionHouseBot: Updating Auction House...");
        AuctionHouseBot::instance()->Update();
        LOG_INFO("module", "AuctionHouseBot: Auction House Updated.");
        handler->PSendSysMessage("AuctionHouseBot: Auction House Updated.");
        return true;
    }

    static bool HandleAHBotReloadCommand(ChatHandler* handler, char const* /*args*/)
    {
        LOG_INFO("module", "AuctionHouseBot: Reloading Config...");
        handler->PSendSysMessage("AuctionHouseBot: Reloading Config...");

        // Reload config file with isReload = true
        sConfigMgr->LoadModulesConfigs(true, false);
        AuctionHouseBot::instance()->InitializeConfiguration();
        AuctionHouseBot::instance()->PopulateItemCandidatesAndProportions();

        if (sConfigMgr->GetOption<bool>("AuctionHouseBot.AdvancedListingRules.UseDropRates.Enabled", true))
        {
            auctionbot->PopulateQuestRewardItemIDs();
            auctionbot->PopulateItemDropChances();
        }

        LOG_INFO("module", "AuctionHouseBot: Config reloaded.");
        handler->PSendSysMessage("AuctionHouseBot: Config reloaded.");        
        return true;
    }

    static bool HandleAHBotEmptyCommand(ChatHandler* handler, char const* /*args*/)
    {
        LOG_INFO("module", "AuctionHouseBot: Emptying Auction House...");
        handler->PSendSysMessage("AuctionHouseBot: Emptying Auction House...");
        AuctionHouseBot::instance()->EmptyAuctionHouses();
        AuctionHouseBot::instance()->CleanupExpiredAuctionItems(); // Must go after EmptyAuctionHouses()
        LOG_INFO("module", "AuctionHouseBot: Auction Houses Emptied.");
        handler->PSendSysMessage("AuctionHouseBot: Auction Houses Emptied.");
        return true;
    }

    static bool HandleAHBotHelpCommand(ChatHandler* handler, char const* /*args*/)
    {
        handler->PSendSysMessage("AuctionHouseBot commands:");
        handler->PSendSysMessage("  .ahbot reload - Reloads configuration");
        handler->PSendSysMessage("  .ahbot empty  - Removes all AuctionHouseBot auctions");
        handler->PSendSysMessage("  .ahbot update - Runs an update cycle");
        return true;
    }

    static bool HandleCreateSellersCommand(ChatHandler* handler, char const* args)
    {
        // Operator path: create complete characters through Player::Create on the dedicated
        // AHTRADE account. Every name is validated before any character is created, and the
        // account must start empty, so a retry cannot leave a partial roster.
        std::vector<std::string> names;
        std::istringstream input(args ? args : "");
        for (std::string name; input >> name;)
            names.push_back(name);

        uint32 const maxSellers = sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM);
        if (names.empty() || names.size() > maxSellers)
        {
            handler->PSendSysMessage("Usage: ahbot create-sellers Name1 [Name2 ...] (at most {} names)", maxSellers);
            return false;
        }

        uint32 accountId = AccountMgr::GetId("AHTRADE");
        if (!accountId || AccountMgr::GetCharactersCount(accountId) != 0)
        {
            handler->PSendSysMessage("AHTRADE account must exist and contain no characters");
            return false;
        }

        std::set<std::string> seen;
        for (std::string& name : names)
        {
            if (!normalizePlayerName(name) || ObjectMgr::CheckPlayerName(name, true) != CHAR_NAME_SUCCESS)
            {
                handler->PSendSysMessage("Seller name {} failed validation", name);
                return false;
            }
            if (!seen.insert(name).second)
            {
                handler->PSendSysMessage("Seller name {} is listed twice", name);
                return false;
            }
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
            stmt->SetData(0, name);
            if (CharacterDatabase.Query(stmt))
            {
                handler->PSendSysMessage("Seller name {} already exists", name);
                return false;
            }
        }

        // Only the name shows in the auction house; race and class just need to be valid.
        struct SellerLook
        {
            uint8 race;
            uint8 playerClass;
        };
        SellerLook const looks[] = {
            {RACE_HUMAN, CLASS_WARRIOR}, {RACE_DWARF, CLASS_HUNTER}, {RACE_GNOME, CLASS_MAGE},
            {RACE_NIGHTELF, CLASS_DRUID}, {RACE_ORC, CLASS_SHAMAN}, {RACE_TROLL, CLASS_PRIEST},
            {RACE_TAUREN, CLASS_WARRIOR}, {RACE_UNDEAD_PLAYER, CLASS_ROGUE}
        };

        WorldSession session(accountId, "AHTRADE", 0, nullptr, SEC_PLAYER,
            EXPANSION_WRATH_OF_THE_LICH_KING, 0, LOCALE_enUS, 0, false, false, 0, true);
        for (size_t i = 0; i < names.size(); ++i)
        {
            SellerLook const& look = looks[i % std::size(looks)];
            CharacterCreateInfo info(names[i], look.race, look.playerClass,
                i % 2 ? GENDER_FEMALE : GENDER_MALE, 0, 0, 0, 0, 0);
            Player player(&session);
            player.GetMotionMaster()->Initialize();
            if (!player.Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &info))
            {
                handler->PSendSysMessage("Seller character creation failed; inspect the partial roster");
                return false;
            }
            player.setCinematic(2);
            player.SetAtLoginFlag(AT_LOGIN_NONE);
            player.SaveToDB(true, false);
            sCharacterCache->AddCharacterCacheEntry(player.GetGUID(), accountId,
                player.GetName(), player.getGender(), player.getRace(),
                player.getClass(), player.GetLevel());
            handler->PSendSysMessage("Created auction seller {} (GUID {})",
                player.GetName(), player.GetGUID().GetCounter());
            player.CleanupsBeforeDelete();
        }
        return true;
    }
};

void AddAHBotScripts()
{
    new AHBot_WorldScript();
    new AHBot_AuctionHouseScript();
    new AHBot_MailScript();
    new AHBot_CommandScript();
}
