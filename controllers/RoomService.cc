#include "RoomService.h"
#include<algorithm>
#include<random>
#include<drogon/drogon.h>
#include"DeckController.h"
#include<drogon/orm/Exception.h>

BattleRoom::ActionResult  BattleRoom::applyOperation(int64_t userId, const Operation &operation)
{
    EventVector new_events;
    ActionError actionerr; 
    std::lock_guard<std::mutex> lock_guard(mutex_);
    int request_player;
    if (userId == players[0].userId)request_player = 0;
    else if (userId == players[1].userId)request_player = 1;
    else return {ActionError::PlayerNotInRoom, version_, {}};
    {//check if the request is processed
        auto it = processed_op[request_player].find(operation.requestId);
        if(it!=processed_op[request_player].end())return it->second;
    }
    {
    bool is_turn;
    switch(status_){
    case RoomStatus::Playing:
        is_turn = userId == players[currentPlayer % 2].userId;
        if(is_turn || userId == players[(currentPlayer + 1) % 2].userId)actionerr = ActionError::None;
        else actionerr = ActionError::PlayerNotInRoom;
        break;
    case RoomStatus::Finished:
        actionerr = ActionError::RoomFinished;
        break;
    case RoomStatus::Preparing:
        actionerr = ActionError::PlayerNotInRoom;
        break;
    }
    if(actionerr != ActionError::None)return {actionerr, version_, new_events};
    if(operation.expectedVersion != version_)
        return {ActionError::StaleVersion, version_, new_events};
    if(operation.type!=OperationType::Surrender && !is_turn)
        return {ActionError::NotYourTurn, version_, new_events};
    }

    version_++;

    switch(operation.type){
        case BattleRoom::OperationType::PlayCard:
        if(!game_update_playcard_(new_events, operation)){
            actionerr = ActionError::InvalidCard;
            break;
        }
        if(request_player)testinfo.player_2_play_card = true;
        else testinfo.player_1_play_card = true;
        break;
        case BattleRoom::OperationType::Attack:
        break;
        case BattleRoom::OperationType::EndTurn:
        game_update_endturn_(new_events);
        if(status_ == RoomStatus::Finished)break;
        game_update_startturn_(new_events);
        break;
        case BattleRoom::OperationType::Surrender:
        set_finished_((request_player + 1) % 2);
        new_events.push_back(create_event(RoomEventType::GameEnd, EventVisibility::Public, -1, -1, -1, -1, winner));
        break;
    }

    //copy to event queue
    for(auto & elem : new_events){
        if(events.size() >= max_event_stored)events.pop_front();
        events.push_back(elem);
    }

    {//remove events that are invisible
    auto oppvisible = request_player == 0 ? EventVisibility::PlayerTwoOnly : EventVisibility::PlayerOneOnly;

    for(size_t index = 0; index < new_events.size(); )
        if(new_events[index].visibility == oppvisible)
            new_events.erase(new_events.begin() + index, new_events.begin() + index + 1);
        else ++index;
    }

    if(actionerr!=ActionError::None)--version_;
    ActionResult res = {actionerr, version_, std::move(new_events)};
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
    is_viewer_valid = players[0].userId==viewerId || players[1].userId == viewerId;
    if(!is_viewer_valid)return ret;
    get_events_impl(ret, sequence, viewerId);
    return ret;
}

Json::Value BattleRoom::getEventsAfterJson(uint64_t sequence, uint64_t viewerId, bool &is_viewer_valid)
{
    EventVector ev;
    Json::Value ret;
    mutex_.lock();
    is_viewer_valid = players[0].userId==viewerId || players[1].userId == viewerId;
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
    for(int i=0;i<2;++i){
    players[i].userId = match.players[i];
    players[i].deckId = match.decks[i];
    try{
        bool success;
        players[i].deck = DeckController::GetDeckById(players[i].userId, players[i].deckId, success);
        if(!success)return false;
    }catch(const drogon::orm::DrogonDbException& e){
        return false;
    }
    if(players[i].deck.size()<4)return false;
    std::shuffle(players[i].deck.begin(), players[i].deck.end(),g);
        for(auto k = 0; k< 3; ++k){
            players[i].hand.push_back(create_instance_(players[i].deck.back(), i, CardInstance::ZoneHand));
            players[i].deck.pop_back();
        }
    }
    currentPlayer = rand()%2;
    players[(currentPlayer + 1) % 2].hand.push_back(create_instance_(
        players[(currentPlayer + 1)%2].deck.back(),
        (currentPlayer + 1) % 2,
        CardInstance::ZoneHand));
    players[(currentPlayer + 1) % 2].deck.pop_back();
    matchId_ = match.matchId;
    status_ = RoomStatus::Playing;
    playerLeft_[0] = false;
    playerLeft_[1] = false;
    hasFinishedAt_ = false;
    {
        PlayerState& player = players[currentPlayer % 2];
        if(player.maxMana < player.maxMana_max)player.maxMana++;
        player.mana = player.maxMana;
        if(currentPlayer % 2 == 0)
            this->events.push_back(this->create_event(RoomEventType::Player1_Turn, EventVisibility::Public, -1, -1, -1, -1, 0));
        else
            this->events.push_back(this->create_event(RoomEventType::Player2_Turn, EventVisibility::Public, -1, -1, -1, -1, 0));
    }
    return true;
}

bool BattleRoom::isFinishedFor(std::chrono::steady_clock::duration duration)const
{
    std::lock_guard<std::mutex> lock_guard(mutex_);
    return status_ == RoomStatus::Finished && hasFinishedAt_ &&
        std::chrono::steady_clock::now() - finishedAt_ >= duration;
}

bool BattleRoom::markPlayerLeft(int64_t playerid, bool &both_left)
{
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if(playerid == players[0].userId)playerLeft_[0] = true;
    else if(playerid == players[1].userId)playerLeft_[1] = true;
    else return false;
    both_left = status_ == RoomStatus::Finished && playerLeft_[0] && playerLeft_[1];
    return true;
}

BattleRoom::QuickStateGettingStruct BattleRoom::quickStateGetting(int64_t playerid)
{
    QuickStateGettingStruct ret{0};
    std::lock_guard<std::mutex> guard(mutex_);
    if(players[0].userId==playerid || players[1].userId == playerid){
        ret.function_success = true;
        ret.roomId = roomId_;
        ret.matchId = matchId_;
        ret.state = status_;
        ret.opponentId = playerid == players[0].userId? players[1].userId :players[0].userId;
    }
    return ret;
}

bool BattleRoom::getSnapshotBin(int64_t viewerId, RoomSnapshot &snapshot)
{
    std::lock_guard<std::mutex> lock_guard(mutex_);

    if(players[0].userId != viewerId && players[1].userId != viewerId)
        return false;
    get_snapshot_impl(snapshot, viewerId);
    return true;
}

Json::Value BattleRoom::getSnapshotJson(int64_t viewerId, bool &is_vaild)
{
    Json::Value ret;
    RoomSnapshot snapshot;
    mutex_.lock();
    is_vaild = players[0].userId==viewerId || players[1].userId == viewerId;
    if(!is_vaild){
        mutex_.unlock();
        ret["status"] =  "ERROR";
        ret["message"] = "Viewer not in battle room";
        return ret;
    }
    get_snapshot_impl(snapshot, viewerId);
    mutex_.unlock();
    ret["state"] =  "SUCCESS";
    ret["room_id"] = static_cast<Json::Int64>(snapshot.roomId);
    ret["match_id"] = static_cast<Json::Int64>(snapshot.matchId);
    ret["version"] = static_cast<Json::UInt64>(snapshot.version);
    ret["last_sequence"] = static_cast<Json::UInt64>(snapshot.last_sequence);
    ret["current_player"] = static_cast<Json::Int>(snapshot.currentPlayer);
    ret["winner_id"] = static_cast<Json::Int64>(snapshot.winnerId);
    switch(snapshot.status){
    case RoomStatus::Finished:
        ret["room_state"] = "finished";
        break;
    case RoomStatus::Playing:
        ret["room_state"] = "playing";
        break;
    case RoomStatus::Preparing:
        ret["room_state"] = "perparing";
        break;
    default:
        ret["room_state"] = "unknow";
        break;
    }
    for(int i = 0; i <2; ++i){
        std::string k1 = "player_" + std::to_string(i);
        ret[k1.c_str()]["user_id"] = static_cast<Json::Int64>(snapshot.players[i].userId);
        ret[k1.c_str()]["health"] = static_cast<Json::Int>(snapshot.players[i].health);
        ret[k1.c_str()]["mana"] = static_cast<Json::Int>(snapshot.players[i].mana);
        ret[k1.c_str()]["max_mana"] = static_cast<Json::Int>(snapshot.players[i].maxMana);
        ret[k1.c_str()]["deck_count"] = static_cast<Json::Int>(snapshot.players[i].deck_count);
        ret[k1.c_str()]["hand_count"] = static_cast<Json::Int>(snapshot.players[i].hand_count);
        ret[k1.c_str()]["board"] = Json::Value(Json::arrayValue);
        for(auto elem : snapshot.players[i].board){
            Json::Value card;
            card["instance_id"] = static_cast<Json::Int64>(elem);
            auto it = instance_map.find(elem);
            if(it != instance_map.end())
                card["card_id"] = static_cast<Json::Int64>(it->second.cardId);
            else
                card["card_id"] = Json::Value();
            ret[k1.c_str()]["board"].append(std::move(card));
        }
    }
    ret["my_hand"] = Json::Value(Json::arrayValue);
    for(auto elem : snapshot.my_hand){
        Json::Value card;
        card["instance_id"] = static_cast<Json::Int64>(elem);
        auto it = instance_map.find(elem);
        if(it != instance_map.end())
            card["card_id"] = static_cast<Json::Int64>(it->second.cardId);
        else
            card["card_id"] = Json::Value();
        ret["my_hand"].append(std::move(card));
    }
    return ret;
}

Json::Value BattleRoom::eventListToJson(const EventVector &events)
{
    Json::Value ret(Json::arrayValue);
    for(auto elem : events){
        Json::Value something;
        something["actor_id"] = elem.actorId;
        something["card_id"] = static_cast<Json::Int64>(elem.cardId);
        something["card_instance"] = static_cast<Json::Int64>(elem.instanceId);
        something["target_id"] = static_cast<Json::Int64>(elem.targetId);
        something["room_version"] = elem.roomVersion;
        something["sequence"] = elem.sequence;
        switch (elem.type)
        {
        case RoomEventType::DestoryCard:
            something["type"] = "card_destory";
            break;
        case RoomEventType::DiscardCard:
            something["type"] = "card_discard";
            break;
        case RoomEventType::EventErr_snapshot_required:
            something["type"] = "error_require_snapshot";
            break;
        case RoomEventType::GameEnd:
            something["type"] = "game_end";
            break;
        case RoomEventType::PlayCard:
            something["type"] = "card_play";
            break;
        case RoomEventType::Player1_DrawCard:
            something["type"] = "player_1_drawcard";
            break;
        case RoomEventType::Player1_Fatigue:
            something["type"] = "player_1_fatigue";
            break;
        case RoomEventType::Player1_Turn:
            something["type"] = "player_1_start_turn";
            break;
        case RoomEventType::Player2_DrawCard:
            something["type"] = "player_2_drawcard";
            break;
        case RoomEventType::Player2_Fatigue:
            something["type"] = "player_2_fatigue";
            break;
        case RoomEventType::Player2_Turn:
            something["type"] = "player_2_start_turn";
            break;
        default:
            something["type"] = "unknown";
            break;
        }
        something["value"] = elem.value;
        ret.append(something);
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
    for(auto & elem : events)
        if(elem.sequence > seq){
            switch (elem.visibility)
            {
            case EventVisibility::Public:
            ret.push_back(elem);
            break;
            case EventVisibility::PlayerOneOnly:
            if(viewer==players[0].userId)
            ret.push_back(elem);
            break;
            case EventVisibility::PlayerTwoOnly:
            if(viewer==players[1].userId)
            ret.push_back(elem);
            break;
            }
        }
}

bool BattleRoom::game_update_playcard_(EventVector &_ev, const Operation &op)
{
    PlayerState& player = players[currentPlayer%2];
    auto it = std::find(player.hand.begin(), player.hand.end(), op.cardInstanceId);
    if(it == player.hand.end())
        return false;
    auto instanceIt = instance_map.find(op.cardInstanceId);
    if(instanceIt == instance_map.end())
        return false;
    int64_t cardId = instanceIt->second.cardId;

    //TODO: return false if invalid target
    _ev.push_back(create_event(
        RoomEventType::PlayCard,
        EventVisibility::Public,
        cardId,
        player.userId,
        op.cardInstanceId,
        op.targetId,
        1));
    instanceIt->second.zone = CardInstance::ZoneGraveyard;
    player.hand.erase(it);
    //TODO: process the card
    return true;
}

void BattleRoom::get_snapshot_impl(RoomSnapshot &ret, int64_t viewer)
{
    ret.roomId = roomId_;
    ret.matchId = matchId_;
    ret.currentPlayer = currentPlayer;
    ret.version = version_;
    if(events.empty())ret.last_sequence = 0;
    else ret.last_sequence = events.back().sequence;
    ret.status = status_;
    ret.winnerId = -1;
    if(status_==RoomStatus::Finished){
        if(winner != -1)ret.winnerId = players[winner%2].userId;
    }
    for(int i = 0; i < 2; ++i){
        ret.players[i].userId = players[i].userId;
        ret.players[i].mana = players[i].mana;
        ret.players[i].maxMana = players[i].maxMana;
        ret.players[i].health = players[i].health;
        ret.players[i].deck_count = players[i].deck.size();
        ret.players[i].hand_count = players[i].hand.size();
        ret.players[i].board = players[i].board;
        if(players[i].userId == viewer)
            ret.my_hand = players[i].hand;
    }
}

void BattleRoom::game_update_endturn_(EventVector &_ev)
{
    //TODO: check if any card are triggered at end of the turn
    currentPlayer++;
    if(currentPlayer % 2 == 0)
        _ev.push_back(this->create_event(RoomEventType::Player1_Turn, EventVisibility::Public, -1, -1, -1, -1, 0));
    else
        _ev.push_back(this->create_event(RoomEventType::Player2_Turn, EventVisibility::Public, -1, -1, -1, -1, 0));
}

void BattleRoom::game_update_startturn_(EventVector &_ev)
{
    PlayerState& player = players[currentPlayer % 2];
    if(player.maxMana < player.maxMana_max)player.maxMana++;
    player.mana = player.maxMana;
    if(!player_drawcard_(_ev, currentPlayer) && player.health<=0){
        set_finished_((currentPlayer + 1) % 2);
        _ev.emplace_back(create_event(RoomEventType::GameEnd, EventVisibility::Public, -1, -1, -1, -1, winner));
    }
}

void BattleRoom::set_finished_(int winner_index)
{
    if(status_ != RoomStatus::Finished){
        status_ = RoomStatus::Finished;
        hasFinishedAt_ = true;
        finishedAt_ = std::chrono::steady_clock::now();
    }
    winner = winner_index;
}

bool BattleRoom::player_drawcard_(EventVector& _ev, int which)
{
    PlayerState& player = players[which % 2];
    if(!player.deck.empty()){
        int64_t cardId = player.deck.back();
        int64_t instanceId = create_instance_(cardId, which % 2, CardInstance::ZoneHand);
        if(which % 2 == 0)
            _ev.emplace_back(create_event(
                RoomEventType::Player1_DrawCard, 
                EventVisibility::PlayerOneOnly, 
                cardId,
                player.userId,
                instanceId,
                -1,
                1));
        else
            _ev.emplace_back(create_event( 
                RoomEventType::Player2_DrawCard, 
                EventVisibility::PlayerTwoOnly,
                cardId,
                player.userId,
                instanceId,
                -1,
                1));
        if(player.hand.size() < player.hand_max)
            player.hand.push_back(instanceId);
        else{
            instance_map[instanceId].zone = CardInstance::ZoneDiscard;
            _ev.emplace_back(create_event( 
                RoomEventType::DestoryCard, 
                EventVisibility::Public,
                cardId,
                player.userId,
                instanceId,
                -1,
                1));
        }
        player.deck.pop_back();
        //TODO: Process card triggered by draw card
        return true;
    }else{
        if(which % 2 == 0)
                _ev.emplace_back(create_event( 
                    RoomEventType::Player1_Fatigue, 
                    EventVisibility::Public, 
                    -1, player.userId, -1, -1, player.fatigue_damage));
            else
                _ev.emplace_back(create_event( 
                    RoomEventType::Player2_Fatigue, 
                    EventVisibility::Public,
                    -1, player.userId, -1, -1, player.fatigue_damage));
        player.health -= player.fatigue_damage;
        player.fatigue_damage++;
        return false;
    }
}

int64_t BattleRoom::create_instance_(int64_t cardId, int ownerIndex, int zone)
{
    int64_t instanceId = instance_id_counter++;
    CardInstance instance{};
    instance.instanceId = instanceId;
    instance.cardId = cardId;
    instance.ownerIndex = static_cast<int8_t>(ownerIndex);
    instance.zone = static_cast<int8_t>(zone);
    instance.type = 0;
    instance.attack = 0;
    instance.health = 0;
    instance.maxHealth = 0;
    instance.flags.exhausted = 0;
    instance.flags.canAttack = 0;
    instance.flags.reserved = 0;
    instance_map[instanceId] = instance;
    return instanceId;
}

bool BattleRoom::destroy_instance_(int64_t instanceId)
{
    auto it = instance_map.find(instanceId);
    if(it == instance_map.end())
        return false;
    it->second.zone = CardInstance::ZoneDiscard;
    return true;
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
