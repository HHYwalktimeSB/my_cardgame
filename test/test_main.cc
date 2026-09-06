#define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include "BattleEngine.h"

DROGON_TEST(BattleEnginePlayCard)
{
    cardgame::BattleState state;
    state.status = cardgame::RoomStatus::Playing;
    state.currentPlayer = 0;
    state.players[0].userId = 1;
    state.players[0].mana = 1;
    state.cardDefinitions[10] = {
        2,
        3,
        1,
        cardgame::CardType::Minion,
        {}};

    auto instanceId = cardgame::BattleEngine::createInstance(
        state,
        10,
        0,
        cardgame::CardZone::Hand);
    state.players[0].hand.push_back(instanceId);

    cardgame::Operation operation{
        cardgame::OperationType::PlayCard,
        cardgame::TargetType::Minion,
        1,
        0,
        instanceId,
        -1};
    uint64_t nextSequence = 1;
    auto result = cardgame::BattleEngine::resolve(
        state, 0, operation, 1, nextSequence);

    CHECK(result.error == cardgame::ActionError::None);
    CHECK(state.players[0].hand.empty());
    CHECK(state.players[0].mana == 0);
    REQUIRE(state.players[0].board.size() == 1);
    CHECK(state.players[0].board.front() == instanceId);
    CHECK(state.instances.at(instanceId).zone == cardgame::CardZone::Board);
    CHECK(state.instances.at(instanceId).attack == 2);
    CHECK(state.instances.at(instanceId).health == 3);
    REQUIRE(result.generatedEvents.size() == 1);
    CHECK(result.generatedEvents.front().type == cardgame::RoomEventType::PlayCard);
    CHECK(result.generatedEvents.front().roomVersion == 1);
    CHECK(result.generatedEvents.front().sequence == 1);
}

DROGON_TEST(BattleEngineRejectsInvalidOperationWithoutMutation)
{
    cardgame::BattleState state;
    state.status = cardgame::RoomStatus::Playing;
    state.currentPlayer = 0;
    state.players[0].userId = 1;

    cardgame::Operation operation{
        cardgame::OperationType::PlayCard,
        cardgame::TargetType::Minion,
        1,
        0,
        99,
        -1};
    uint64_t nextSequence = 1;
    auto result = cardgame::BattleEngine::resolve(
        state, 0, operation, 1, nextSequence);

    CHECK(result.error == cardgame::ActionError::InvalidCard);
    CHECK(state.players[0].hand.empty());
    CHECK(state.instances.empty());
    CHECK(result.generatedEvents.empty());
    CHECK(nextSequence == 1);
}

DROGON_TEST(BattleEngineMinionAttack)
{
    cardgame::BattleState state;
    state.status = cardgame::RoomStatus::Playing;
    state.players[0].userId = 1;
    state.players[1].userId = 2;
    state.cardDefinitions[10] = {
        3, 2, 1, cardgame::CardType::Minion, {}};
    state.cardDefinitions[20] = {
        2, 3, 1, cardgame::CardType::Minion, {}};

    auto attackerId = cardgame::BattleEngine::createInstance(
        state, 10, 0, cardgame::CardZone::Board);
    auto targetId = cardgame::BattleEngine::createInstance(
        state, 20, 1, cardgame::CardZone::Board);
    state.instances.at(attackerId).exhausted = false;
    state.players[0].board.push_back(attackerId);
    state.players[1].board.push_back(targetId);

    cardgame::Operation operation{
        cardgame::OperationType::Attack,
        cardgame::TargetType::Minion,
        1,
        0,
        attackerId,
        targetId};
    uint64_t nextSequence = 1;
    auto result = cardgame::BattleEngine::resolve(
        state, 0, operation, 1, nextSequence);

    CHECK(result.error == cardgame::ActionError::None);
    CHECK(state.players[0].board.empty());
    CHECK(state.players[1].board.empty());
    CHECK(state.instances.at(attackerId).zone == cardgame::CardZone::Graveyard);
    CHECK(state.instances.at(targetId).zone == cardgame::CardZone::Graveyard);
    REQUIRE(result.generatedEvents.size() == 3);
    CHECK(result.generatedEvents[0].type == cardgame::RoomEventType::MinionAttack);
    CHECK(result.generatedEvents[1].type == cardgame::RoomEventType::MinionDead);
    CHECK(result.generatedEvents[2].type == cardgame::RoomEventType::MinionDead);
}

DROGON_TEST(BattleEngineHeroAttackEndsGame)
{
    cardgame::BattleState state;
    state.status = cardgame::RoomStatus::Playing;
    state.players[0].userId = 1;
    state.players[1].userId = 2;
    state.players[1].health = 3;
    state.cardDefinitions[10] = {
        4, 2, 1, cardgame::CardType::Minion, {}};

    auto attackerId = cardgame::BattleEngine::createInstance(
        state, 10, 0, cardgame::CardZone::Board);
    state.instances.at(attackerId).exhausted = false;
    state.players[0].board.push_back(attackerId);

    cardgame::Operation operation{
        cardgame::OperationType::Attack,
        cardgame::TargetType::Hero,
        1,
        0,
        attackerId,
        state.players[1].userId};
    uint64_t nextSequence = 1;
    auto result = cardgame::BattleEngine::resolve(
        state, 0, operation, 1, nextSequence);

    CHECK(result.error == cardgame::ActionError::None);
    CHECK(state.players[1].health == -1);
    CHECK(state.status == cardgame::RoomStatus::Finished);
    CHECK(state.winner == 0);
    REQUIRE(result.generatedEvents.size() == 2);
    CHECK(result.generatedEvents[0].targetType == cardgame::TargetType::Hero);
    CHECK(result.generatedEvents[1].type == cardgame::RoomEventType::GameEnd);
}

DROGON_TEST(BattleEngineBattlecryTriggersDeathrattle)
{
    cardgame::BattleState state;
    state.status = cardgame::RoomStatus::Playing;
    state.currentPlayer = 0;
    state.players[0].userId = 1;
    state.players[1].userId = 2;
    state.players[0].mana = 1;
    state.players[1].health = 20;

    cardgame::CardDefinition battlecryCard{
        1, 1, 1, cardgame::CardType::Minion, {}};
    battlecryCard.effects[static_cast<size_t>(
        cardgame::EffectTrigger::Battlecry)] = {{
            cardgame::EffectType::Damage,
            cardgame::EffectTarget::Selected,
            2}};
    state.cardDefinitions[10] = battlecryCard;

    cardgame::CardDefinition deathrattleCard{
        0, 2, 1, cardgame::CardType::Minion, {}};
    deathrattleCard.effects[static_cast<size_t>(
        cardgame::EffectTrigger::Deathrattle)] = {{
            cardgame::EffectType::Heal,
            cardgame::EffectTarget::FriendlyHero,
            3}};
    state.cardDefinitions[20] = deathrattleCard;

    auto playedId = cardgame::BattleEngine::createInstance(
        state, 10, 0, cardgame::CardZone::Hand);
    auto targetId = cardgame::BattleEngine::createInstance(
        state, 20, 1, cardgame::CardZone::Board);
    state.players[0].hand.push_back(playedId);
    state.players[1].board.push_back(targetId);

    cardgame::Operation operation{
        cardgame::OperationType::PlayCard,
        cardgame::TargetType::Minion,
        1,
        0,
        playedId,
        targetId};
    uint64_t nextSequence = 1;
    auto result = cardgame::BattleEngine::resolve(
        state, 0, operation, 1, nextSequence);

    CHECK(result.error == cardgame::ActionError::None);
    CHECK(state.players[1].board.empty());
    CHECK(state.instances.at(targetId).zone == cardgame::CardZone::Graveyard);
    CHECK(state.players[1].health == 23);
    REQUIRE(result.generatedEvents.size() == 4);
    CHECK(result.generatedEvents[0].type == cardgame::RoomEventType::PlayCard);
    CHECK(result.generatedEvents[1].type == cardgame::RoomEventType::EffectDamage);
    CHECK(result.generatedEvents[2].type == cardgame::RoomEventType::MinionDead);
    CHECK(result.generatedEvents[3].type == cardgame::RoomEventType::EffectHeal);
}

int main(int argc, char** argv) 
{
    using namespace drogon;

    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();

    // Start the main loop on another thread
    std::thread thr([&]() {
        // Queues the promise to be fulfilled after starting the loop
        app().getLoop()->queueInLoop([&p1]() { p1.set_value(); });
        app().run();
    });

    // The future is only satisfied after the event loop started
    f1.get();
    int status = test::run(argc, argv);

    // Ask the event loop to shutdown and wait
    app().getLoop()->queueInLoop([]() { app().quit(); });
    thr.join();
    return status;
}
