#include "RoomService.h"
#include "BattleEngine.h"
#include "BattleJsonWriter.h"
#include<algorithm>
#include<optional>
#include<random>
#include<sstream>
#include<string_view>
#include<drogon/drogon.h>
#include"DeckController.h"
#include<drogon/orm/Exception.h>
#include<models/Cards.h>

using namespace drogon_model::cardgame_db;
using namespace drogon::orm;

namespace
{

std::string_view event_type_name(BattleRoom::RoomEventType type)
{
    switch(type)
    {
    case BattleRoom::RoomEventType::DestoryCard:
        return "card_destory";
    case BattleRoom::RoomEventType::DiscardCard:
        return "card_discard";
    case BattleRoom::RoomEventType::EventErr_snapshot_required:
        return "error_require_snapshot";
    case BattleRoom::RoomEventType::GameEnd:
        return "game_end";
    case BattleRoom::RoomEventType::PlayCard:
        return "card_play";
    case BattleRoom::RoomEventType::Player1_DrawCard:
        return "player_1_drawcard";
    case BattleRoom::RoomEventType::Player1_Fatigue:
        return "player_1_fatigue";
    case BattleRoom::RoomEventType::Player1_Turn:
        return "player_1_start_turn";
    case BattleRoom::RoomEventType::Player2_DrawCard:
        return "player_2_drawcard";
    case BattleRoom::RoomEventType::Player2_Fatigue:
        return "player_2_fatigue";
    case BattleRoom::RoomEventType::Player2_Turn:
        return "player_2_start_turn";
    case BattleRoom::RoomEventType::MinionAttack:
        return "minion_attack";
    case BattleRoom::RoomEventType::MinionDead:
        return "minion_dead";
    case BattleRoom::RoomEventType::EffectDamage:
        return "effect_damage";
    case BattleRoom::RoomEventType::EffectHeal:
        return "effect_heal";
    case BattleRoom::RoomEventType::EffectBuff:
        return "effect_buff";
    }
    return "unknown";
}

void append_event_json(
    cardgame::BattleJsonWriter &writer,
    const BattleRoom::RoomEvent &event)
{
    writer.beginObject();
    writer.key("actor_id");
    writer.integer(event.actorId);
    writer.key("card_id");
    writer.integer(event.cardId);
    writer.key("card_instance");
    writer.integer(event.instanceId);
    writer.key("target_id");
    writer.integer(event.targetId);
    writer.key("target_type");
    writer.string(event.targetType == BattleRoom::TargetType::Hero
        ? "hero"
        : "minion");
    writer.key("room_version");
    writer.integer(event.roomVersion);
    writer.key("sequence");
    writer.integer(event.sequence);
    writer.key("type");
    writer.string(event_type_name(event.type));
    writer.key("value");
    writer.integer(event.value);
    writer.endObject();
}

std::string_view room_state_name(BattleRoom::RoomStatus status)
{
    switch(status)
    {
    case BattleRoom::RoomStatus::Finished: return "finished";
    case BattleRoom::RoomStatus::Playing: return "playing";
    case BattleRoom::RoomStatus::Preparing: return "perparing";
    }
    return "unknow";
}

void append_snapshot_card(
    cardgame::BattleJsonWriter &writer,
    const BattleRoom::RoomSnapshot::Card &card,
    bool includeStats)
{
    writer.beginObject();
    writer.key("instance_id");
    writer.integer(card.instanceId);
    writer.key("card_id");
    if(card.valid)writer.integer(card.cardId);
    else writer.nullValue();
    if(includeStats && card.valid)
    {
        writer.key("attack");
        writer.integer(card.attack);
        writer.key("health");
        writer.integer(card.health);
        writer.key("max_health");
        writer.integer(card.maxHealth);
        writer.key("exhausted");
        writer.boolean(card.exhausted);
    }
    writer.endObject();
}

void append_snapshot_player(
    cardgame::BattleJsonWriter &writer,
    const BattleRoom::RoomSnapshot::PublicPlayerState &player)
{
    writer.beginObject();
    writer.key("user_id");
    writer.integer(player.userId);
    writer.key("health");
    writer.integer(player.health);
    writer.key("mana");
    writer.integer(player.mana);
    writer.key("max_mana");
    writer.integer(player.maxMana);
    writer.key("deck_count");
    writer.integer(player.deck_count);
    writer.key("hand_count");
    writer.integer(player.hand_count);
    writer.key("board");
    writer.beginArray();
    for(const auto &card : player.board)
        append_snapshot_card(writer, card, true);
    writer.endArray();
    writer.endObject();
}

std::string serialize_websocket_events(const std::string &eventArray)
{
    std::string response;
    response.reserve(eventArray.size() + 64);
    response += R"({"state":"SUCCESS","transport":"websocket","events":)";
    response += eventArray;
    response += '}';
    return response;
}

bool have_same_events(
    const BattleRoom::EventVector &left,
    const BattleRoom::EventVector &right)
{
    if(left.size() != right.size())return false;
    return std::equal(
        left.begin(),
        left.end(),
        right.begin(),
        [](const auto &leftEvent, const auto &rightEvent) {
            return leftEvent.sequence == rightEvent.sequence;
        });
}

std::optional<cardgame::CardType> parse_card_type(const std::string &type)
{
    if(type == "minion")return cardgame::CardType::Minion;
    if(type == "spell")return cardgame::CardType::Spell;
    return std::nullopt;
}

std::optional<cardgame::EffectType> parse_effect_type(const std::string &type)
{
    if(type == "damage")return cardgame::EffectType::Damage;
    if(type == "heal")return cardgame::EffectType::Heal;
    if(type == "buff")return cardgame::EffectType::Buff;
    return std::nullopt;
}

std::optional<cardgame::EffectTarget> parse_effect_target(const std::string &target)
{
    if(target == "selected")return cardgame::EffectTarget::Selected;
    if(target == "self")return cardgame::EffectTarget::Self;
    if(target == "friendly_hero")return cardgame::EffectTarget::FriendlyHero;
    if(target == "enemy_hero")return cardgame::EffectTarget::EnemyHero;
    return std::nullopt;
}

bool parse_effect_list(
    const Json::Value &root,
    const char *name,
    cardgame::EffectTrigger trigger,
    cardgame::CardEffects &effects)
{
    if(!root.isMember(name))return true;
    const Json::Value &items = root[name];
    if(!items.isArray())return false;
    auto &output = effects[static_cast<size_t>(trigger)];
    for(const auto &item : items)
    {
        if(!item.isObject() || !item["type"].isString() ||
           !item["target"].isString() || !item["value"].isInt())
            return false;
        auto type = parse_effect_type(item["type"].asString());
        auto target = parse_effect_target(item["target"].asString());
        const int value = item["value"].asInt();
        if(!type || !target || value <= 0 ||
           (*type == cardgame::EffectType::Buff &&
            (*target == cardgame::EffectTarget::FriendlyHero ||
             *target == cardgame::EffectTarget::EnemyHero)) ||
           (trigger == cardgame::EffectTrigger::Deathrattle &&
            *target == cardgame::EffectTarget::Selected))
            return false;
        output.push_back({*type, *target, value});
    }
    return true;
}

bool parse_effect_json(const std::string &text, cardgame::CardEffects &effects)
{
    if(text.empty())return true;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    Json::Value root;
    if(!Json::parseFromStream(builder, stream, &root, &errors))return false;
    if(root.isNull())return true;
    if(!root.isObject())return false;
    return parse_effect_list(
               root, "battlecry", cardgame::EffectTrigger::Battlecry, effects) &&
           parse_effect_list(
               root, "deathrattle", cardgame::EffectTrigger::Deathrattle, effects);
}

}

RoomService::SerializedEventArray RoomService::serializeEventArray(
    const BattleRoom::EventVector &events)
{
    constexpr size_t maxEventJsonSize = 320;
    cardgame::BattleJsonWriter writer(
        events.size() * maxEventJsonSize + 2);
    writer.beginArray();
    for(const auto &event : events)
        append_event_json(writer, event);
    writer.endArray();
    return std::make_shared<const std::string>(writer.take());
}

std::string RoomService::serializeSnapshot(
    const BattleRoom::RoomSnapshot &snapshot)
{
    constexpr size_t fixedSnapshotJsonSize = 768;
    constexpr size_t maxBoardCardJsonSize = 192;
    constexpr size_t maxHandCardJsonSize = 80;
    const size_t boardCardCount =
        snapshot.players[0].board.size() +
        snapshot.players[1].board.size();
    cardgame::BattleJsonWriter writer(
        fixedSnapshotJsonSize +
        boardCardCount * maxBoardCardJsonSize +
        snapshot.my_hand.size() * maxHandCardJsonSize);
    writer.beginObject();
    writer.key("state");
    writer.string("SUCCESS");
    writer.key("room_id");
    writer.integer(snapshot.roomId);
    writer.key("match_id");
    writer.integer(snapshot.matchId);
    writer.key("version");
    writer.integer(snapshot.version);
    writer.key("last_sequence");
    writer.integer(snapshot.last_sequence);
    writer.key("current_player");
    writer.integer(snapshot.currentPlayer);
    writer.key("winner_id");
    writer.integer(snapshot.winnerId);
    writer.key("room_state");
    writer.string(room_state_name(snapshot.status));
    writer.key("player_0");
    append_snapshot_player(writer, snapshot.players[0]);
    writer.key("player_1");
    append_snapshot_player(writer, snapshot.players[1]);
    writer.key("my_hand");
    writer.beginArray();
    for(const auto &card : snapshot.my_hand)
        append_snapshot_card(writer, card, false);
    writer.endArray();
    writer.endObject();
    return writer.take();
}

BattleRoom::ActionResult  BattleRoom::applyOperation(int64_t userId, const Operation &operation)
{
    std::lock_guard<std::mutex> lock_guard(mutex_);
    int request_player;
    if (userId == state_.players[0].userId)request_player = 0;
    else if (userId == state_.players[1].userId)request_player = 1;
    else return {ActionError::PlayerNotInRoom, version_, {}};
    {//check if the request is processed
        auto it = processed_op[request_player].find(operation.requestId);
        if(it!=processed_op[request_player].end())return it->second;
    }
    if(operation.expectedVersion != version_)
        return {ActionError::StaleVersion, version_, {}};

    const RoomStatus previous_status = state_.status;
    auto resolution = cardgame::BattleEngine::resolve(
        state_,
        request_player,
        operation,
        version_ + 1,
        nextSequence);
    if(resolution.error == ActionError::RoomFinished ||
       resolution.error == ActionError::PlayerNotInRoom ||
       resolution.error == ActionError::NotYourTurn)
        return {resolution.error, version_, {}};

    if(resolution.error == ActionError::None)
    {
        version_++;
        if(previous_status != RoomStatus::Finished &&
           state_.status == RoomStatus::Finished)
        {
            hasFinishedAt_ = true;
            finishedAt_ = std::chrono::steady_clock::now();
        }
    }

    //copy to event queue
    for(auto & elem : resolution.generatedEvents){
        if(events.size() >= max_event_stored)events.pop_front();
        events.push_back(elem);
    }

    {//remove events that are invisible
    auto oppvisible = request_player == 0 ? EventVisibility::PlayerTwoOnly : EventVisibility::PlayerOneOnly;

    for(size_t index = 0; index < resolution.generatedEvents.size(); )
        if(resolution.generatedEvents[index].visibility == oppvisible)
            resolution.generatedEvents.erase(
                resolution.generatedEvents.begin() + index,
                resolution.generatedEvents.begin() + index + 1);
        else ++index;
    }

    ActionResult res = {
        resolution.error,
        version_,
        std::move(resolution.generatedEvents)};
    while(processed_op_q[request_player].size()>=max_requestid_stored){
        processed_op[request_player].erase(processed_op_q[request_player].front());
        processed_op_q[request_player].pop();
    }
    processed_op_q[request_player].push(operation.requestId);
    processed_op[request_player][operation.requestId] = res;
    return res;

}

BattleRoom::EventVector BattleRoom::getEventsAfter(uint64_t sequence, uint64_t viewerId, bool& is_viewer_valid)
{
    EventVector ret;
    std::lock_guard<std::mutex> lock_guard(mutex_);
    is_viewer_valid = state_.players[0].userId==viewerId || state_.players[1].userId == viewerId;
    if(!is_viewer_valid)return ret;
    get_events_impl(ret, sequence, viewerId);
    return ret;
}

Json::Value BattleRoom::getEventsAfterJson(uint64_t sequence, uint64_t viewerId, bool &is_viewer_valid)
{
    EventVector ev;
    Json::Value ret;
    mutex_.lock();
    is_viewer_valid = state_.players[0].userId==viewerId || state_.players[1].userId == viewerId;
    if(!is_viewer_valid){
        mutex_.unlock();
        ret["state"] = "ERROR";
        ret["message"] = "player not in room";
        return ret;
    }
    get_events_impl(ev, sequence, viewerId);
    mutex_.unlock();
    ret["state"] = "SUCCESS";
    ret["events"] = eventListToJson(ev);
    return ret;
}

bool BattleRoom::init_with_match_info(const MatchInfo &match)
{
    std::random_device rd;
    std::mt19937 g(rd());
    std::lock_guard<std::mutex> lock_guard(mutex_);
    std::unordered_set<int64_t> all_cards;
    try
    {
        for(int i = 0; i < 2; ++i)
        {
            state_.players[i].userId = match.players[i];
            state_.players[i].deckId = match.decks[i];
            bool success;
            state_.players[i].deck = DeckController::GetDeckById(
                state_.players[i].userId,
                state_.players[i].deckId,
                success);
            if(!success || state_.players[i].deck.size() < 4)
                return false;
            for(int64_t cardId : state_.players[i].deck)
                all_cards.insert(cardId);
        }

        auto dbClient = app().getDbClient("db");
        Mapper<Cards> cardMapper(dbClient);
        for(int64_t cardId : all_cards)
        {
            auto card = cardMapper.findByPrimaryKey(cardId);
            auto cardType = parse_card_type(card.getValueOfCardType());
            if(!cardType)
                return false;

            cardgame::CardEffects effects;
            if(card.getEffectJson() &&
               !parse_effect_json(card.getValueOfEffectJson(), effects))
                return false;

            state_.cardDefinitions[cardId] = {
                card.getValueOfAttack(),
                card.getValueOfHealth(),
                card.getValueOfManaCost(),
                *cardType,
                std::move(effects)};
        }
    }
    catch(const drogon::orm::DrogonDbException &e)
    {
        return false;
    }

    for(int i = 0; i < 2; ++i)
    {
        std::shuffle(
            state_.players[i].deck.begin(),
            state_.players[i].deck.end(),
            g);
        for(int cardCount = 0; cardCount < 3; ++cardCount)
        {
            state_.players[i].hand.push_back(
                cardgame::BattleEngine::createInstance(
                    state_,
                    state_.players[i].deck.back(),
                    i,
                    cardgame::CardZone::Hand));
            state_.players[i].deck.pop_back();
        }
    }
    state_.currentPlayer = rand()%2;
    state_.players[(state_.currentPlayer + 1) % 2].hand.push_back(
        cardgame::BattleEngine::createInstance(
            state_,
            state_.players[(state_.currentPlayer + 1)%2].deck.back(),
            (state_.currentPlayer + 1) % 2,
            cardgame::CardZone::Hand));
    state_.players[(state_.currentPlayer + 1) % 2].deck.pop_back();
    matchId_ = match.matchId;
    state_.status = RoomStatus::Playing;
    playerLeft_[0] = false;
    playerLeft_[1] = false;
    hasFinishedAt_ = false;
    {
        PlayerState& player = state_.players[state_.currentPlayer % 2];
        if(player.maxMana < player.maxMana_max)player.maxMana++;
        player.mana = player.maxMana;
        if(state_.currentPlayer % 2 == 0)
            this->events.push_back(this->create_event(RoomEventType::Player1_Turn, EventVisibility::Public, -1, -1, -1, -1, 0));
        else
            this->events.push_back(this->create_event(RoomEventType::Player2_Turn, EventVisibility::Public, -1, -1, -1, -1, 0));
    }
    return true;
}

bool BattleRoom::isFinishedFor(std::chrono::steady_clock::duration duration)const
{
    std::lock_guard<std::mutex> lock_guard(mutex_);
    return state_.status == RoomStatus::Finished && hasFinishedAt_ &&
        std::chrono::steady_clock::now() - finishedAt_ >= duration;
}

bool BattleRoom::markPlayerLeft(int64_t playerid, bool &both_left)
{
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if(playerid == state_.players[0].userId)playerLeft_[0] = true;
    else if(playerid == state_.players[1].userId)playerLeft_[1] = true;
    else return false;
    both_left = state_.status == RoomStatus::Finished && playerLeft_[0] && playerLeft_[1];
    return true;
}

BattleRoom::QuickStateGettingStruct BattleRoom::quickStateGetting(int64_t playerid)
{
    QuickStateGettingStruct ret{0};
    std::lock_guard<std::mutex> guard(mutex_);
    if(state_.players[0].userId==playerid || state_.players[1].userId == playerid){
        ret.function_success = true;
        ret.roomId = roomId_;
        ret.matchId = matchId_;
        ret.state = state_.status;
        ret.opponentId = playerid == state_.players[0].userId
            ? state_.players[1].userId
            : state_.players[0].userId;
    }
    return ret;
}

bool BattleRoom::getSnapshotBin(int64_t viewerId, RoomSnapshot &snapshot)
{
    std::lock_guard<std::mutex> lock_guard(mutex_);

    if(state_.players[0].userId != viewerId && state_.players[1].userId != viewerId)
        return false;
    get_snapshot_impl(snapshot, viewerId);
    return true;
}

Json::Value BattleRoom::eventListToJson(const EventVector &events)
{
    Json::Value ret(Json::arrayValue);
    for(const auto &elem : events){
        Json::Value something;
        something["actor_id"] = elem.actorId;
        something["card_id"] = static_cast<Json::Int64>(elem.cardId);
        something["card_instance"] = static_cast<Json::Int64>(elem.instanceId);
        something["target_id"] = static_cast<Json::Int64>(elem.targetId);
        something["target_type"] = elem.targetType == TargetType::Hero
            ? "hero"
            : "minion";
        something["room_version"] = elem.roomVersion;
        something["sequence"] = elem.sequence;
        something["type"] = event_type_name(elem.type).data();
        something["value"] = elem.value;
        ret.append(std::move(something));
    }
    return ret;
}

void BattleRoom::get_events_impl(EventVector &ret, uint64_t seq, int64_t viewer)
{
    if(!events.empty() && seq < events.front().sequence - 1){
        ret.push_back({events.back().sequence, events.back().roomVersion, 
            RoomEventType::EventErr_snapshot_required, EventVisibility::Public, 
            -1, -1, -1, -1, 0 });
        return;
    }
    for(const auto & elem : events)
        if(elem.sequence > seq){
            switch (elem.visibility)
            {
            case EventVisibility::Public:
            ret.push_back(elem);
            break;
            case EventVisibility::PlayerOneOnly:
            if(viewer==state_.players[0].userId)
            ret.push_back(elem);
            break;
            case EventVisibility::PlayerTwoOnly:
            if(viewer==state_.players[1].userId)
            ret.push_back(elem);
            break;
            }
        }
}

void BattleRoom::get_snapshot_impl(RoomSnapshot &ret, int64_t viewer)
{
    ret.roomId = roomId_;
    ret.matchId = matchId_;
    ret.currentPlayer = state_.players[state_.currentPlayer % 2].userId;
    ret.version = version_;
    if(events.empty())ret.last_sequence = 0;
    else ret.last_sequence = events.back().sequence;
    ret.status = state_.status;
    ret.winnerId = -1;
    if(state_.status==RoomStatus::Finished){
        if(state_.winner != -1)
            ret.winnerId = state_.players[state_.winner%2].userId;
    }
    for(int i = 0; i < 2; ++i){
        ret.players[i].userId = state_.players[i].userId;
        ret.players[i].mana = state_.players[i].mana;
        ret.players[i].maxMana = state_.players[i].maxMana;
        ret.players[i].health = state_.players[i].health;
        ret.players[i].deck_count = state_.players[i].deck.size();
        ret.players[i].hand_count = state_.players[i].hand.size();
        ret.players[i].board.clear();
        ret.players[i].board.reserve(state_.players[i].board.size());
        for(int64_t instanceId : state_.players[i].board)
        {
            RoomSnapshot::Card card{};
            card.instanceId = instanceId;
            auto instance = state_.instances.find(instanceId);
            card.valid = instance != state_.instances.end();
            if(card.valid)
            {
                card.cardId = instance->second.cardId;
                card.attack = instance->second.attack;
                card.health = instance->second.health;
                card.maxHealth = instance->second.maxHealth;
                card.exhausted = instance->second.exhausted;
            }
            ret.players[i].board.push_back(card);
        }
        if(state_.players[i].userId == viewer)
        {
            ret.my_hand.clear();
            ret.my_hand.reserve(state_.players[i].hand.size());
            for(int64_t instanceId : state_.players[i].hand)
            {
                RoomSnapshot::Card card{};
                card.instanceId = instanceId;
                auto instance = state_.instances.find(instanceId);
                card.valid = instance != state_.instances.end();
                if(card.valid)card.cardId = instance->second.cardId;
                ret.my_hand.push_back(card);
            }
        }
    }
}

std::shared_ptr<BattleRoom> RoomService::createRoom(const MatchInfo &match)
{
    if(match.players[1]==match.players[0])return nullptr;
    auto ret = std::make_shared<BattleRoom>();
    auto init_success = ret->init_with_match_info(match);
    std::lock_guard<std::mutex> guard(mutex_);
    auto has_active_room = [this](int64_t playerid) {
        auto it = playerRooms.find(playerid);
        if(it == playerRooms.end())return false;
        auto roomIt = rooms.find(it->second);
        return roomIt != rooms.end() && !roomIt->second->isFinished();
    };
    if(has_active_room(match.players[0]) || has_active_room(match.players[1]))return nullptr;
    if(init_success){
        rooms[room_id_counter] = ret;
        playerRooms[match.players[0]] = room_id_counter;
        playerRooms[match.players[1]] = room_id_counter;
        polls[room_id_counter] = PollStruct();
        polls[room_id_counter].players[0] = match.players[0];
        polls[room_id_counter].players[1] = match.players[1];
        ret->setRoomId(room_id_counter);
        room_id_counter++;
        return ret;
    }
    return nullptr;
}

std::shared_ptr<BattleRoom> RoomService::findRoom(int64_t roomId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = rooms.find(roomId);
    if(it!=rooms.end())return it->second;
    return nullptr;
}

std::shared_ptr<BattleRoom> RoomService::findRoomByPlayer(int64_t userId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = playerRooms.find(userId);
    if(it!=playerRooms.end()){
        auto it2 = rooms.find(it->second);
        if(it2!=rooms.end())return it2->second;
        return nullptr;
    }
    return nullptr;
}

bool RoomService::is_player_in_room(int64_t userid)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = playerRooms.find(userid);
    if(it == playerRooms.end())return false;
    auto roomIt = rooms.find(it->second);
    return roomIt != rooms.end() && !roomIt->second->isFinished();
}

bool RoomService::removeRoom(int64_t roomId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = rooms.find(roomId);
    if(it!=rooms.end())
    {
        auto playerIt = playerRooms.find(it->second->getPlayer1Id());
        if(playerIt != playerRooms.end() && playerIt->second == roomId)
            playerRooms.erase(playerIt);
        playerIt = playerRooms.find(it->second->getPlayer2Id());
        if(playerIt != playerRooms.end() && playerIt->second == roomId)
            playerRooms.erase(playerIt);
        polls.erase(roomId);
        websocketSubscribers.erase(roomId);
        rooms.erase(it);
        return true;
    }
    return false;
}

bool RoomService::removeFinishedRoomIfExpired(int64_t roomId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = rooms.find(roomId);
    if(it == rooms.end() || !it->second->isFinishedFor(std::chrono::minutes(5)))
        return false;
    auto playerIt = playerRooms.find(it->second->getPlayer1Id());
    if(playerIt != playerRooms.end() && playerIt->second == roomId)
        playerRooms.erase(playerIt);
    playerIt = playerRooms.find(it->second->getPlayer2Id());
    if(playerIt != playerRooms.end() && playerIt->second == roomId)
        playerRooms.erase(playerIt);
    polls.erase(roomId);
    websocketSubscribers.erase(roomId);
    rooms.erase(it);
    return true;
}

bool RoomService::playerLeaveRoom(int64_t roomId, int64_t userId)
{
    std::shared_ptr<BattleRoom> room;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = rooms.find(roomId);
        if(it == rooms.end())return false;
        room = it->second;
    }
    bool both_left = false;
    if(!room->markPlayerLeft(userId, both_left))return false;
    if(both_left)removeRoom(roomId);
    return true;
}

void RoomService::scheduleFinishedRoomCleanup(int64_t roomId)
{
    drogon::app().getLoop()->runAfter(300.0, [roomId](){
        RoomService::GetServer().removeFinishedRoomIfExpired(roomId);
    });
}

static RoomService RoomServiceServer;

RoomService &RoomService::GetServer()
{
    return RoomServiceServer;
}

std::pair<bool, uint64_t> RoomService::registerPoll(int roomId, int64_t userId, std::function<void()> &&callback)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = polls.find(roomId);
    if(it!=polls.end()){
        if(it->second.players[0]==userId&& !it->second.is_registered[0]){
            it->second.is_registered[0] = true;
            it->second.toks[0] = poll_tok_gen;
            it->second.callback[0] = std::move(callback);
            return std::make_pair(true, poll_tok_gen++);
        }
        if(it->second.players[1]==userId&& !it->second.is_registered[1]){
            it->second.is_registered[1] = true;
            it->second.toks[1] = poll_tok_gen;
            it->second.callback[1] = std::move(callback);
            return std::make_pair(true, poll_tok_gen++);
        }
    }
    return std::make_pair(false, 0);
}

void RoomService::invokePolls(int roomId)
{
    std::function<void()> cb[2];
    cb[0] = []()->void{};
    cb[1] = []()->void{};
    {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = polls.find(roomId);
    if(it!=polls.end()){
        if(it->second.is_registered[0]){
            it->second.is_registered[0] = false;
            cb[0] = std::move(it->second.callback[0]);
        }
        if(it->second.is_registered[1]){
            it->second.is_registered[1] = false;
            it->second.callback[1]();
            cb[1] = std::move(it->second.callback[1]);
        }
    }
    }
    cb[0]();
    cb[1]();
}

void RoomService::invokePollWithToken(int64_t roomId, uint64_t tok)
{
    std::function<void()> cb = []()->void{};
    {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = polls.find(roomId);
    if(it!=polls.end()){
        if(it->second.is_registered[0] && it->second.toks[0]==tok){
            it->second.is_registered[0] = false;
            cb = std::move(it->second.callback[0]);
        }
        if(it->second.is_registered[1] && it->second.toks[1]==tok){
            it->second.is_registered[1] = false;
            cb = std::move(it->second.callback[1]);
        }
    }
    }
    cb();
}

void RoomService::registerWebSocket(
    int64_t roomId,
    int64_t userId,
    uint64_t sequence,
    const drogon::WebSocketConnectionPtr &connection)
{
    auto subscriber = std::make_shared<WebSocketSubscriber>();
    subscriber->userId = userId;
    subscriber->sequence = sequence;
    subscriber->connection = connection;
    std::shared_ptr<BattleRoom> room;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto roomIt = rooms.find(roomId);
        if(roomIt == rooms.end())return;
        room = roomIt->second;
        websocketSubscribers[roomId].push_back(subscriber);
    }
    sendWebSocketEvents(room, subscriber);
}

void RoomService::unregisterWebSocket(
    const drogon::WebSocketConnectionPtr &connection)
{
    std::lock_guard<std::mutex> guard(mutex_);
    for(auto roomIt = websocketSubscribers.begin();
        roomIt != websocketSubscribers.end();)
    {
        auto &subscribers = roomIt->second;
        subscribers.erase(
            std::remove_if(
                subscribers.begin(),
                subscribers.end(),
                [&connection](const auto &subscriber) {
                    auto current = subscriber->connection.lock();
                    return !current || current == connection;
                }),
            subscribers.end());
        if(subscribers.empty())roomIt = websocketSubscribers.erase(roomIt);
        else ++roomIt;
    }
}

void RoomService::syncWebSocket(
    const drogon::WebSocketConnectionPtr &connection,
    uint64_t sequence)
{
    std::shared_ptr<BattleRoom> room;
    std::shared_ptr<WebSocketSubscriber> matchedSubscriber;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for(auto &[roomId, subscribers] : websocketSubscribers)
        {
            for(const auto &subscriber : subscribers)
            {
                if(subscriber->connection.lock() == connection)
                {
                    auto roomIt = rooms.find(roomId);
                    if(roomIt != rooms.end())room = roomIt->second;
                    matchedSubscriber = subscriber;
                    break;
                }
            }
            if(matchedSubscriber)break;
        }
    }
    if(!room || !matchedSubscriber)return;
    {
        std::lock_guard<std::mutex> guard(matchedSubscriber->mutex);
        matchedSubscriber->sequence = sequence;
    }
    sendWebSocketEvents(room, matchedSubscriber);
}

void RoomService::publishWebSocketEvents(
    int64_t roomId,
    const BattleRoom::EventVector &sharedEvents,
    const SerializedEventArray &sharedEventArray)
{
    std::shared_ptr<BattleRoom> room;
    std::vector<std::shared_ptr<WebSocketSubscriber>> subscribers;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto roomIt = rooms.find(roomId);
        if(roomIt == rooms.end())return;
        room = roomIt->second;
        auto subscriberIt = websocketSubscribers.find(roomId);
        if(subscriberIt == websocketSubscribers.end())return;
        auto &storedSubscribers = subscriberIt->second;
        storedSubscribers.erase(
            std::remove_if(
                storedSubscribers.begin(),
                storedSubscribers.end(),
                [](const auto &subscriber) {
                    return subscriber->connection.expired();
                }),
            storedSubscribers.end());
        subscribers = storedSubscribers;
    }
    for(const auto &subscriber : subscribers)
        sendWebSocketEvents(
            room,
            subscriber,
            &sharedEvents,
            sharedEventArray);
}

void RoomService::sendWebSocketEvents(
    const std::shared_ptr<BattleRoom> &room,
    const std::shared_ptr<WebSocketSubscriber> &subscriber,
    const BattleRoom::EventVector *sharedEvents,
    const SerializedEventArray &sharedEventArray)
{
    std::lock_guard<std::mutex> guard(subscriber->mutex);
    auto connection = subscriber->connection.lock();
    if(!connection || !connection->connected())return;
    bool validViewer = false;
    auto events = room->getEventsAfter(
        subscriber->sequence,
        subscriber->userId,
        validViewer);
    if(!validViewer)
    {
        connection->shutdown(drogon::CloseCode::kViolation, "player not in room");
        return;
    }
    for(const auto &event : events)
        subscriber->sequence = std::max(
            subscriber->sequence,
            event.sequence);
    auto serializedEvents = sharedEventArray;
    if(!serializedEvents || !sharedEvents ||
       !have_same_events(events, *sharedEvents))
        serializedEvents = serializeEventArray(events);
    connection->send(serialize_websocket_events(*serializedEvents));
}
