#include "BattleEngine.h"

#include <algorithm>
#include <utility>

namespace cardgame
{

BattleResolution BattleEngine::resolve(
    BattleState &state,
    int playerIndex,
    const Operation &operation,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    EventVector events;
    if(state.status == RoomStatus::Finished)
        return {ActionError::RoomFinished, {}};
    if(state.status == RoomStatus::Preparing)
        return {ActionError::PlayerNotInRoom, {}};
    if(operation.type != OperationType::Surrender &&
       playerIndex != state.currentPlayer % 2)
        return {ActionError::NotYourTurn, {}};

    switch(operation.type)
    {
    case OperationType::PlayCard:
    {
        auto error = playCard(
            state, events, operation, roomVersion, nextSequence);
        if(error != ActionError::None)
            return {error, {}};
        break;
    }
    case OperationType::Attack:
        if(!AttackTarget(state, events, operation, roomVersion, nextSequence))
            return {ActionError::InvalidTarget, {}};
        break;
    case OperationType::EndTurn:
        endTurn(state, events, roomVersion, nextSequence);
        if(state.status != RoomStatus::Finished)
            startTurn(state, events, roomVersion, nextSequence);
        break;
    case OperationType::Surrender:
        setFinished(state, (playerIndex + 1) % 2);
        events.push_back(createEvent(
            RoomEventType::GameEnd,
            EventVisibility::Public,
            -1,
            -1,
            -1,
            -1,
            state.winner,
            roomVersion,
            nextSequence));
        break;
    }

    return {ActionError::None, std::move(events)};
}

int64_t BattleEngine::createInstance(
    BattleState &state,
    int64_t cardId,
    int ownerIndex,
    CardZone zone)
{
    const CardDefinition &definition = state.cardDefinitions.at(cardId);
    int64_t instanceId = state.instanceIdCounter++;
    CardInstance instance{};
    instance.instanceId = instanceId;
    instance.cardId = cardId;
    instance.ownerIndex = static_cast<int8_t>(ownerIndex);
    instance.zone = zone;
    instance.attack = definition.attack;
    instance.health = definition.health;
    instance.maxHealth = definition.health;
    instance.exhausted = true;
    state.instances[instanceId] = instance;
    return instanceId;
}

bool BattleEngine::AttackTarget(BattleState &state, EventVector &events, const Operation &operation, uint64_t roomVersion, uint64_t &nextSequence)
{
    auto cardit = state.instances.find(operation.cardInstanceId);
    if(cardit == state.instances.end())
        return false;
    CardInstance& attacker = cardit->second;
    const int attackerIndex = state.currentPlayer % 2;
    const int targetPlayerIndex = (attackerIndex + 1) % 2;
    if(attacker.exhausted ||
       attacker.zone != CardZone::Board ||
       attacker.ownerIndex != attackerIndex ||
       attacker.health <= 0 ||
       attacker.attack <= 0)
        return false;

    if(operation.targetType == TargetType::Hero)
    {
        PlayerState &targetPlayer = state.players[targetPlayerIndex];
        if(operation.targetId != targetPlayer.userId)
            return false;

        attacker.exhausted = true;
        targetPlayer.health -= attacker.attack;
        events.push_back(createEvent(
            RoomEventType::MinionAttack,
            EventVisibility::Public,
            attacker.cardId,
            state.players[attackerIndex].userId,
            attacker.instanceId,
            targetPlayer.userId,
            attacker.attack,
            roomVersion,
            nextSequence,
            TargetType::Hero));
        if(targetPlayer.health <= 0)
        {
            setFinished(state, attackerIndex);
            events.push_back(createEvent(
                RoomEventType::GameEnd,
                EventVisibility::Public,
                -1,
                state.players[attackerIndex].userId,
                -1,
                targetPlayer.userId,
                state.winner,
                roomVersion,
                nextSequence,
                TargetType::Hero));
        }
        return true;
    }

    if(operation.cardInstanceId == operation.targetId)
        return false;
    auto targetit = state.instances.find(operation.targetId);
    if(targetit == state.instances.end())
        return false;
    CardInstance &target = targetit->second;
    if(target.zone != CardZone::Board ||
       target.ownerIndex != targetPlayerIndex ||
       target.health <= 0)
        return false;

    const int attackerDamage = attacker.attack;
    const int targetDamage = target.attack;
    attacker.exhausted = true;
    attacker.health -= targetDamage;
    target.health -= attackerDamage;
    events.push_back(createEvent(RoomEventType::MinionAttack, EventVisibility::Public, 
        attacker.cardId, state.players[attackerIndex].userId,
        attacker.instanceId, operation.targetId, attackerDamage,
        roomVersion, nextSequence, TargetType::Minion));
    std::deque<PendingEffect> pendingEffects;
    auto deadMinions = collectDeadMinions(state);
    destroyDeadMinions(
        state, events, deadMinions, pendingEffects, roomVersion, nextSequence);
    resolveEffects(
        state, events, pendingEffects, roomVersion, nextSequence);
    return true;
}

ActionError BattleEngine::playCard(
    BattleState &state,
    EventVector &events,
    const Operation &operation,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    PlayerState &player = state.players[state.currentPlayer % 2];
    auto handIt = std::find(
        player.hand.begin(), player.hand.end(), operation.cardInstanceId);
    if(handIt == player.hand.end())
        return ActionError::InvalidCard;

    auto instanceIt = state.instances.find(operation.cardInstanceId);
    if(instanceIt == state.instances.end())
        return ActionError::InvalidCard;

    CardInstance &instance = instanceIt->second;
    if(instance.ownerIndex != state.currentPlayer % 2 ||
       instance.zone != CardZone::Hand)
        return ActionError::InvalidCard;

    auto definitionIt = state.cardDefinitions.find(instance.cardId);
    if(definitionIt == state.cardDefinitions.end())
        return ActionError::InvalidCard;

    const CardDefinition &definition = definitionIt->second;
    if(player.mana < definition.manaCost)
        return ActionError::InsufficientMana;
    if(definition.type == CardType::Minion && player.board.size() >= 7)
        return ActionError::BoardFull;
    if(!validateBattlecryTargets(state, instance, definition, operation))
        return ActionError::InvalidTarget;

    player.mana -= definition.manaCost;
    player.hand.erase(handIt);
    if(definition.type == CardType::Minion)
    {
        instance.zone = CardZone::Board;
        instance.exhausted = true;
        player.board.push_back(instance.instanceId);
    }
    else
        instance.zone = CardZone::Graveyard;

    events.push_back(createEvent(
        RoomEventType::PlayCard,
        EventVisibility::Public,
        instance.cardId,
        player.userId,
        operation.cardInstanceId,
        operation.targetId,
        1,
        roomVersion,
        nextSequence,
        operation.targetType));

    std::deque<PendingEffect> pendingEffects;
    enqueueEffects(
        pendingEffects,
        definition,
        EffectTrigger::Battlecry,
        instance,
        operation.targetType,
        operation.targetId);
    resolveEffects(
        state, events, pendingEffects, roomVersion, nextSequence);

    return ActionError::None;
}

void BattleEngine::endTurn(
    BattleState &state,
    EventVector &events,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    state.currentPlayer++;
    events.push_back(createEvent(
        state.currentPlayer % 2 == 0
            ? RoomEventType::Player1_Turn
            : RoomEventType::Player2_Turn,
        EventVisibility::Public,
        -1,
        -1,
        -1,
        -1,
        0,
        roomVersion,
        nextSequence));
}

void BattleEngine::startTurn(
    BattleState &state,
    EventVector &events,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    PlayerState &player = state.players[state.currentPlayer % 2];
    if(player.maxMana < player.maxMana_max)
        player.maxMana++;
    player.mana = player.maxMana;
    for(int64_t instanceId : player.board)
    {
        auto instanceIt = state.instances.find(instanceId);
        if(instanceIt != state.instances.end())
            instanceIt->second.exhausted = false;
    }
    if(!drawCard(
           state,
           events,
           state.currentPlayer,
           roomVersion,
           nextSequence) &&
       player.health <= 0)
    {
        setFinished(state, (state.currentPlayer + 1) % 2);
        events.push_back(createEvent(
            RoomEventType::GameEnd,
            EventVisibility::Public,
            -1,
            -1,
            -1,
            -1,
            state.winner,
            roomVersion,
            nextSequence));
    }
}

bool BattleEngine::drawCard(
    BattleState &state,
    EventVector &events,
    int playerIndex,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    PlayerState &player = state.players[playerIndex % 2];
    if(!player.deck.empty())
    {
        int64_t cardId = player.deck.back();
        int64_t instanceId = createInstance(
            state, cardId, playerIndex % 2, CardZone::Hand);
        events.push_back(createEvent(
            playerIndex % 2 == 0
                ? RoomEventType::Player1_DrawCard
                : RoomEventType::Player2_DrawCard,
            playerIndex % 2 == 0
                ? EventVisibility::PlayerOneOnly
                : EventVisibility::PlayerTwoOnly,
            cardId,
            player.userId,
            instanceId,
            -1,
            1,
            roomVersion,
            nextSequence));
        if(player.hand.size() < static_cast<size_t>(player.hand_max))
            player.hand.push_back(instanceId);
        else
        {
            state.instances[instanceId].zone = CardZone::Discard;
            events.push_back(createEvent(
                RoomEventType::DestoryCard,
                EventVisibility::Public,
                cardId,
                player.userId,
                instanceId,
                -1,
                1,
                roomVersion,
                nextSequence));
        }
        player.deck.pop_back();
        return true;
    }

    events.push_back(createEvent(
        playerIndex % 2 == 0
            ? RoomEventType::Player1_Fatigue
            : RoomEventType::Player2_Fatigue,
        EventVisibility::Public,
        -1,
        player.userId,
        -1,
        -1,
        player.fatigue_damage,
        roomVersion,
        nextSequence));
    player.health -= player.fatigue_damage;
    player.fatigue_damage++;
    return false;
}

void BattleEngine::setFinished(BattleState &state, int winnerIndex)
{
    state.status = RoomStatus::Finished;
    state.winner = winnerIndex;
}

bool BattleEngine::validateBattlecryTargets(
    const BattleState &state,
    const CardInstance &source,
    const CardDefinition &definition,
    const Operation &operation)
{
    const auto &effects = definition.effects[
        static_cast<size_t>(EffectTrigger::Battlecry)];
    for(const auto &effect : effects)
    {
        if(effect.value <= 0)return false;
        if(effect.target == EffectTarget::Self)
        {
            if(definition.type != CardType::Minion)return false;
            continue;
        }
        if(effect.target != EffectTarget::Selected)continue;
        if(operation.targetType == TargetType::Hero)
        {
            if(effect.type == EffectType::Buff)return false;
            if(operation.targetId != state.players[0].userId &&
               operation.targetId != state.players[1].userId)
                return false;
            continue;
        }
        auto targetIt = state.instances.find(operation.targetId);
        if(targetIt == state.instances.end() ||
           targetIt->second.zone != CardZone::Board ||
           targetIt->second.health <= 0 ||
           targetIt->second.instanceId == source.instanceId)
            return false;
    }
    return true;
}

void BattleEngine::enqueueEffects(
    std::deque<PendingEffect> &pendingEffects,
    const CardDefinition &definition,
    EffectTrigger trigger,
    const CardInstance &source,
    TargetType selectedTargetType,
    int64_t selectedTargetId)
{
    for(const auto &effect : definition.effects[static_cast<size_t>(trigger)])
        pendingEffects.push_back({
            effect,
            source.ownerIndex,
            source.instanceId,
            source.cardId,
            selectedTargetType,
            selectedTargetId});
}

void BattleEngine::resolveEffects(
    BattleState &state,
    EventVector &events,
    std::deque<PendingEffect> &pendingEffects,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    int resolvedCount = 0;
    while(!pendingEffects.empty() && resolvedCount++ < EFFECT_RESOLUTION_MAX)
    {
        PendingEffect pending = pendingEffects.front();
        pendingEffects.pop_front();

        CardInstance *targetMinion = nullptr;
        int targetPlayerIndex = -1;
        TargetType targetType = TargetType::Minion;
        int64_t targetId = -1;
        if(pending.effect.target == EffectTarget::Selected)
        {
            targetType = pending.selectedTargetType;
            targetId = pending.selectedTargetId;
            if(targetType == TargetType::Hero)
            {
                for(int i = 0; i < 2; ++i)
                    if(state.players[i].userId == targetId)
                        targetPlayerIndex = i;
            }
            else
            {
                auto targetIt = state.instances.find(targetId);
                if(targetIt != state.instances.end() &&
                   targetIt->second.zone == CardZone::Board &&
                   targetIt->second.health > 0)
                    targetMinion = &targetIt->second;
            }
        }
        else if(pending.effect.target == EffectTarget::Self)
        {
            targetId = pending.sourceInstanceId;
            auto targetIt = state.instances.find(targetId);
            if(targetIt != state.instances.end() &&
               targetIt->second.zone == CardZone::Board &&
               targetIt->second.health > 0)
                targetMinion = &targetIt->second;
        }
        else
        {
            targetType = TargetType::Hero;
            targetPlayerIndex = pending.effect.target == EffectTarget::FriendlyHero
                ? pending.ownerIndex
                : (pending.ownerIndex + 1) % 2;
            targetId = state.players[targetPlayerIndex].userId;
        }

        if((targetType == TargetType::Minion && targetMinion == nullptr) ||
           (targetType == TargetType::Hero && targetPlayerIndex < 0) ||
           (pending.effect.type == EffectType::Buff &&
            targetType == TargetType::Hero))
            continue;

        RoomEventType eventType = RoomEventType::EffectDamage;
        int eventValue = pending.effect.value;
        if(pending.effect.type == EffectType::Damage)
        {
            if(targetMinion)
                targetMinion->health -= pending.effect.value;
            else
                state.players[targetPlayerIndex].health -= pending.effect.value;
        }
        else if(pending.effect.type == EffectType::Heal)
        {
            eventType = RoomEventType::EffectHeal;
            if(targetMinion)
            {
                const int oldHealth = targetMinion->health;
                targetMinion->health = std::min(
                    targetMinion->maxHealth,
                    targetMinion->health + pending.effect.value);
                eventValue = targetMinion->health - oldHealth;
            }
            else
            {
                const int oldHealth = state.players[targetPlayerIndex].health;
                state.players[targetPlayerIndex].health = std::min(
                    30,
                    state.players[targetPlayerIndex].health + pending.effect.value);
                eventValue = state.players[targetPlayerIndex].health - oldHealth;
            }
        }
        else
        {
            eventType = RoomEventType::EffectBuff;
            targetMinion->attack += pending.effect.value;
            targetMinion->health += pending.effect.value;
            targetMinion->maxHealth += pending.effect.value;
        }

        events.push_back(createEvent(
            eventType,
            EventVisibility::Public,
            pending.sourceCardId,
            state.players[pending.ownerIndex].userId,
            pending.sourceInstanceId,
            targetId,
            eventValue,
            roomVersion,
            nextSequence,
            targetType));

        if(targetType == TargetType::Hero &&
           pending.effect.type == EffectType::Damage &&
           state.players[targetPlayerIndex].health <= 0)
        {
            setFinished(state, (targetPlayerIndex + 1) % 2);
            events.push_back(createEvent(
                RoomEventType::GameEnd,
                EventVisibility::Public,
                -1,
                state.players[pending.ownerIndex].userId,
                -1,
                targetId,
                state.winner,
                roomVersion,
                nextSequence,
                TargetType::Hero));
            break;
        }

        auto deadMinions = collectDeadMinions(state);
        destroyDeadMinions(
            state,
            events,
            deadMinions,
            pendingEffects,
            roomVersion,
            nextSequence);
    }
}

std::vector<int64_t> BattleEngine::collectDeadMinions(const BattleState &state)
{
    std::vector<int64_t> deadMinions;
    for(int offset = 0; offset < 2; ++offset)
    {
        const int playerIndex = (state.currentPlayer + offset) % 2;
        for(int64_t instanceId : state.players[playerIndex].board)
        {
            auto instanceIt = state.instances.find(instanceId);
            if(instanceIt != state.instances.end() &&
               instanceIt->second.zone == CardZone::Board &&
               instanceIt->second.health <= 0)
                deadMinions.push_back(instanceId);
        }
    }
    return deadMinions;
}

void BattleEngine::destroyDeadMinions(
    BattleState &state,
    EventVector &events,
    const std::vector<int64_t> &deadMinions,
    std::deque<PendingEffect> &pendingEffects,
    uint64_t roomVersion,
    uint64_t &nextSequence)
{
    if(deadMinions.empty())return;
    for(int64_t instanceId : deadMinions)
    {
        auto instanceIt = state.instances.find(instanceId);
        if(instanceIt != state.instances.end() &&
           instanceIt->second.zone == CardZone::Board &&
           instanceIt->second.health <= 0)
            instanceIt->second.zone = CardZone::Graveyard;
    }
    for(auto &player : state.players)
        player.board.erase(
            std::remove_if(
                player.board.begin(),
                player.board.end(),
                [&state](int64_t instanceId) {
                    return state.instances.at(instanceId).zone == CardZone::Graveyard;
                }),
            player.board.end());

    for(int64_t instanceId : deadMinions)
    {
        CardInstance &instance = state.instances.at(instanceId);
        if(instance.zone != CardZone::Graveyard)continue;
        events.push_back(createEvent(
            RoomEventType::MinionDead,
            EventVisibility::Public,
            instance.cardId,
            state.players[instance.ownerIndex].userId,
            instance.instanceId,
            -1,
            0,
            roomVersion,
            nextSequence));
        auto definitionIt = state.cardDefinitions.find(instance.cardId);
        if(definitionIt != state.cardDefinitions.end())
            enqueueEffects(
                pendingEffects,
                definitionIt->second,
                EffectTrigger::Deathrattle,
                instance,
                TargetType::Minion,
                -1);
    }
}

RoomEvent BattleEngine::createEvent(
    RoomEventType type,
    EventVisibility visibility,
    int64_t cardId,
    int64_t actorId,
    int64_t instanceId,
    int64_t targetId,
    int value,
    uint64_t roomVersion,
    uint64_t &nextSequence,
    TargetType targetType)
{
    return {
        nextSequence++,
        roomVersion,
        type,
        visibility,
        cardId,
        actorId,
        targetId,
        instanceId,
        value,
        targetType};
}

}
