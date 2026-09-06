#ifndef BATTLE_ENGINE_H_
#define BATTLE_ENGINE_H_

#include "BattleState.h"
#include <deque>

namespace cardgame
{

struct BattleResolution
{
    ActionError error;
    EventVector generatedEvents;
};

class BattleEngine
{
public:

    constexpr static int EFFECT_RESOLUTION_MAX = 128;
    static BattleResolution resolve(
        BattleState &state,
        int playerIndex,
        const Operation &operation,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static int64_t createInstance(
        BattleState &state,
        int64_t cardId,
        int ownerIndex,
        CardZone zone);

private:
    struct PendingEffect
    {
        EffectDefinition effect;
        int ownerIndex;
        int64_t sourceInstanceId;
        int64_t sourceCardId;
        TargetType selectedTargetType;
        int64_t selectedTargetId;
    };

    static bool AttackTarget(BattleState &state,
        EventVector &events,
        const Operation &operation,
        uint64_t roomVersion,
        uint64_t &nextSequence);

    static ActionError playCard(
        BattleState &state,
        EventVector &events,
        const Operation &operation,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static void endTurn(
        BattleState &state,
        EventVector &events,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static void startTurn(
        BattleState &state,
        EventVector &events,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static bool drawCard(
        BattleState &state,
        EventVector &events,
        int playerIndex,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static void setFinished(BattleState &state, int winnerIndex);
    static bool validateBattlecryTargets(
        const BattleState &state,
        const CardInstance &source,
        const CardDefinition &definition,
        const Operation &operation);
    static void enqueueEffects(
        std::deque<PendingEffect> &pendingEffects,
        const CardDefinition &definition,
        EffectTrigger trigger,
        const CardInstance &source,
        TargetType selectedTargetType,
        int64_t selectedTargetId);
    static void resolveEffects(
        BattleState &state,
        EventVector &events,
        std::deque<PendingEffect> &pendingEffects,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static std::vector<int64_t> collectDeadMinions(const BattleState &state);
    static void destroyDeadMinions(
        BattleState &state,
        EventVector &events,
        const std::vector<int64_t> &deadMinions,
        std::deque<PendingEffect> &pendingEffects,
        uint64_t roomVersion,
        uint64_t &nextSequence);
    static RoomEvent createEvent(
        RoomEventType type,
        EventVisibility visibility,
        int64_t cardId,
        int64_t actorId,
        int64_t instanceId,
        int64_t targetId,
        int value,
        uint64_t roomVersion,
        uint64_t &nextSequence,
        TargetType targetType = TargetType::Minion);
};

}

#endif
