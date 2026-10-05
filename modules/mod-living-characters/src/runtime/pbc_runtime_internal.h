// SPDX-License-Identifier: GPL-2.0-or-later
// PBC Character System changes, 2026-09-30; upstream attribution in NOTICE.md.

#ifndef PBC_RUNTIME_INTERNAL_H
#define PBC_RUNTIME_INTERNAL_H
#include "pbc_action_input.h"
#include "pbc_action_store.h"
#include "pbc_context.h"
#include "pbc_conversation.h"
#include "pbc_game.h"
#include "pbc_initiative.h"
#include "pbc_service.h"
#include "pbc_worker.h"
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <random>
namespace PBC::RuntimeDetail
{
uint64_t EpochMs();
std::string Identifier();
std::string ReadText(std::string const& path);
std::string ReadPrompt(std::string const& path, std::string const& name);
struct PendingDelivery
{
    DueSegment segment;
    ObservationRecord observation;
    std::vector<std::string> witnesses;
    std::future<bool> prepared;
    std::future<std::optional<SourceVersion>> recorded;
    bool sent = false;
};

struct Scene
{
    std::string id = Identifier();
    GameAudience audience;
    Conversation conversation;
    std::string contribution;
    std::string previousSpeaker;
    std::vector<std::string> transcript;
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::future<bool> admitted;
    std::future<ModelReply> selection;
    std::future<CharacterTurn> generation;
    std::future<bool> persistence;
    std::optional<CharacterTurn> turn;
    std::optional<PendingDelivery> delivery;
    std::map<std::size_t, SourceVersion> delivered;
    std::string chosen;
    bool admittedOk = false;
    bool background = false;
    uint64_t expiresMs = 0;
    ActionFrame actions;
    std::string actionClarification;
    ActionFeedback actionFeedback;
    std::optional<PendingAction> pendingAction;
    std::map<std::string, std::string> interpretedActions;
    std::map<uint64_t, uint64_t> actionVersions;
    uint64_t generationActorGuid = 0;
    uint64_t generationActionVersion = 0;
    std::string generationKnowledgeState;
    uint8_t actionRefreshes = 0;
    bool actionsPending = false;
    std::set<uint64_t> commandedActors;
    std::map<uint64_t, std::vector<ActionRequest>> requestedPerformances;
    bool requestedPerformance = false;
    std::map<uint64_t, std::map<std::string, std::pair<ActionRequest, ActionOutcome>>> actionOutcomes;
    InitiativeFrame initiative;
    bool initiativeUsed = false;
};

class Runtime
{
public:
    bool Start();
    void Stop();
    void Update(uint32_t diff);
    void Chat(Player* sender, uint32_t type, uint32_t language, std::string const& text, Player* receiver,
              Channel* channel, bool ambient = false, ObjectGuid addressed = ObjectGuid::Empty,
              bool nativeEmote = false);
    void Cancel(std::string const& actor);
    bool Command(Player* player, std::string const& command);
    void WorldEvent(Player* subject, std::string const& text, bool partyOnly, bool interruptDialogue, Unit* enemy);

private:
    void Enrich(GameAudience& audience) const;
    std::string KnowledgeState(GameActor const& actor, ObjectGuid anchor) const;
    bool StoreActors(GameAudience const& audience);
    std::vector<std::string> Witnesses(GameAudience const& audience) const;
    void Tick(Scene& scene);
    void Abort(Scene& scene, std::string const& reason = "cancelled");
    GameActor* Speaker(Scene& scene, std::string const& id);
    CharacterInput Input(Scene const& scene, GameActor const& actor) const;
    GameAudience DeliveryAudience(Scene const& scene, GameActor const& actor, Segment const& segment) const;
    void Background();
    void RecordWorldEvent(Player* subject, std::string const& text, bool partyOnly, bool interruptDialogue);
    void CombatOpening(Player* subject, PlayerbotDialogue::Entity const& enemy);
    void ActionResult(ActionRequest const& request, ActionOutcome const& outcome);
    void FailureDialogue(GameAudience audience, std::string actorId, std::string detail);
    ActionStatus PrepareAction(ActionRequest const& request);
    CharacterStore _store;
    BudgetStore _ledger;
    std::unique_ptr<ModelGateway> _model;
    std::unique_ptr<CharacterService> _characters;
    std::unique_ptr<Worker> _storage;
    std::unique_ptr<Worker> _inference;
    std::unique_ptr<ActionCoordinator> _actions;
    ActionStore _actionStore;
    std::string _runId = Identifier();
    std::map<std::string, std::future<bool>> _actionPreparations;
    uint64_t _inputSequence = EpochMs();
    std::map<uint64_t, uint64_t> _nextInitiative;
    std::vector<std::unique_ptr<Scene>> _scenes;
    std::map<uint64_t, PendingAction> _pendingActions;
    std::map<uint64_t, RecentCombatOrder> _recentCombatOrders;
    std::map<std::string, std::pair<GameAudience, uint64_t>> _actionAudiences;
    std::vector<std::tuple<GameAudience, std::string, std::string>> _actionNotices;
    NpcDefinitions _definitions;
    KnowledgeCatalogue _knowledge;
    uint32_t _knowledgeBytes = 3600;
    std::string _realmPhase;
    uint64_t _now = 0;
    uint64_t _epochStart = EpochMs();
    uint32_t _spacing = 2000;
    uint32_t _readingWordsPerMinute = 150;
    uint8_t _maxTurns = 4;
    bool _delivering = false;
    uint64_t _nextMemoryScan = 0;
    uint64_t _nextAmbient = 0;
    uint64_t _lastHumanActivity = 0;
    uint32_t _memoryInactiveMs = 300000;
    uint32_t _memoryTokens = 4096;
    uint32_t _ambientMinimum = 1200000;
    uint32_t _ambientMaximum = 2400000;
    uint32_t _ambientQuiet = 120000;
    bool _ambientEnabled = false;
    bool _combatEnabled = true;
    uint32_t _combatChance = 10;
    uint32_t _combatCooldown = 180000;
    uint64_t _nextCombat = 0;
    std::map<uint64_t, PlayerbotDialogue::Entity> _combatAttempted;
    std::set<ObjectGuid> _combatOpened;
    bool _recorded = false;
    std::future<std::vector<std::string>> _pendingMemories;
    std::future<bool> _memory;
    std::string _activeMemoryActor;
    std::map<std::string, uint64_t> _memoryRetryAfter;
    std::vector<std::string> _memoryActors;
    std::mt19937 _random{std::random_device{}()};
    struct CommandReply
    {
        ObjectGuid player;
        std::future<std::string> text;
    };
    std::vector<CommandReply> _commands;
    std::vector<std::future<bool>> _observations;
    struct WorldEventInput
    {
        ObjectGuid subject;
        std::string text;
        bool partyOnly;
        bool interruptDialogue;
        PlayerbotDialogue::Entity enemy;
    };
    std::mutex _worldEventsMutex;
    std::vector<WorldEventInput> _worldEvents;
};

}  // namespace PBC::RuntimeDetail
#endif
