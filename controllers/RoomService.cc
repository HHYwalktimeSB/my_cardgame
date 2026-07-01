#include "RoomService.h"
#include<algorithm>
#include<random>

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
        if(operation.cardInstanceId < 0 || 
            operation.cardInstanceId >= players[request_player].hand.size()){
                //currently I didn't make a unique id for each card in hand, so I just use its index as id
                actionerr = ActionError::InvalidCard;
                break;
            }
        if(!game_update_playcard_(new_events, operation)){
            actionerr = ActionError::InvalidTarget;
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
        status_ = BattleRoom::RoomStatus::Finished;
        winner = (request_player + 1) % 2;
        new_events.push_back(create_event(RoomEventType::GameEnd, EventVisibility::Public, -1, winner));
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
    get_events_impl(ret, viewerId, is_viewer_valid);
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
    get_events_impl(ev, viewerId, is_viewer_valid);
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
    players[i].deck.resize(30, 1);
    //TODO: fill player.deck  with deck info in datebase
    //return false for invalid deck
    std::shuffle(players[i].deck.begin(), players[i].deck.end(),g);
        for(auto k = 0; k< 3; ++k){
            players[i].hand.push_back(players[i].deck.back());
            players[i].deck.pop_back();
        }
    }
    currentPlayer = rand()%2;
    players[(currentPlayer + 1) % 2].hand.push_back(players[(currentPlayer + 1)%2].deck.back());
    players[(currentPlayer + 1) % 2].deck.pop_back();
    matchId_ = match.matchId;
    status_ = RoomStatus::Playing;
    return true;
}

bool BattleRoom::getSnapshotBin(int64_t viewerId, RoomSnapshot &snapshot)
{
    std::lock_guard<std::mutex> lock_guard(mutex_);

    if(players[0].userId==viewerId || players[1].userId == viewerId)
        return false;
    get_snapshot_impl(snapshot, viewerId);
    return true;
}

Json::Value BattleRoom::getSnapshotJson(int viewerId, bool &is_vaild)
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
    ret["status"] =  "SUCCESS";
    ret["room_id"] = static_cast<Json::Int64>(snapshot.roomId);
    ret["match_id"] = static_cast<Json::Int64>(snapshot.matchId);
    for(int i = 0; i <2; ++i){
        std::string k1 = "player_" + std::to_string(i);
        ret[k1.c_str()]["user_id"] = static_cast<Json::Int64>(snapshot.players[i].userId);
        ret[k1.c_str()]["health"] = static_cast<Json::Int>(snapshot.players[i].health);
        ret[k1.c_str()]["mana"] = static_cast<Json::Int>(snapshot.players[i].mana);
        ret[k1.c_str()]["max_mana"] = static_cast<Json::Int>(snapshot.players[i].maxMana);
        ret[k1.c_str()]["deck_count"] = static_cast<Json::Int>(snapshot.players[i].deck_count);
        ret[k1.c_str()]["hand_count"] = static_cast<Json::Int>(snapshot.players[i].hand_count);
        ret[k1.c_str()]["board"] = Json::Value(Json::arrayValue);
        for(auto elem : snapshot.players[i].board)
            ret[k1.c_str()]["board"].append(static_cast<Json::Int64>(elem));
    }
    ret["my_hand"] = Json::Value(Json::arrayValue);
    for(auto elem : snapshot.my_hand)
        ret["my_hand"].append(static_cast<Json::Int64>(elem));
    return ret;
}

Json::Value BattleRoom::eventListToJson(const EventVector &events)
{
    Json::Value ret(Json::arrayValue);
    for(auto elem : events){
        Json::Value something;
        something["actor_id"] = elem.actorId;
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
            0, 0 });
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
    //TODO: return false if invalid target
    _ev.push_back(create_event(RoomEventType::PlayCard, EventVisibility::Public, player.hand[op.cardInstanceId], 1));
    player.hand.erase(player.hand.begin() + op.cardInstanceId);
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
    ret.last_sequence = events.back().sequence;
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
    if(testinfo.player_1_play_card && testinfo.player_2_play_card){
        //currently I'm testing the code, 
        //after two player have played a card, the game will end with draw
        status_ = RoomStatus::Finished;
        _ev.push_back(create_event(RoomEventType::GameEnd, EventVisibility::Public, -1, -1));
        return;
    }
    currentPlayer++;
    if(currentPlayer % 2 == 0)_ev.push_back(this->create_event(RoomEventType::Player1_Turn, EventVisibility::Public, -1, 0));
    else _ev.push_back(this->create_event(RoomEventType::Player2_Turn, EventVisibility::Public, -1, 0));
}

void BattleRoom::game_update_startturn_(EventVector &_ev)
{
    PlayerState& player = players[currentPlayer % 2];
    if(player.maxMana < player.maxMana_max)player.maxMana++;
    player.mana = player.maxMana;
    if(!player_drawcard_(_ev, currentPlayer) && player.health<=0){
        status_ = BattleRoom::RoomStatus::Finished;
        winner = (currentPlayer + 1) % 2;
        _ev.emplace_back(create_event(RoomEventType::GameEnd, EventVisibility::Public, -1, winner));
    }
}

bool BattleRoom::player_drawcard_(EventVector& _ev, int which)
{
    PlayerState& player = players[which % 2];
    if(!player.deck.empty()){
        if(which % 2 == 0)
            _ev.emplace_back(create_event(
                RoomEventType::Player1_DrawCard, 
                EventVisibility::PlayerOneOnly, 
                player.deck.back(), 1));
        else
            _ev.emplace_back(create_event( 
                RoomEventType::Player2_DrawCard, 
                EventVisibility::PlayerTwoOnly,
                player.deck.back(), 1));
        if(player.hand.size() < player.hand_max)
            player.hand.push_back(player.deck.back());
        else
            _ev.emplace_back(create_event( 
                RoomEventType::DestoryCard, 
                EventVisibility::Public,
                player.deck.back(), 1));
        player.deck.pop_back();
        //TODO: Process card triggered by draw card
        return true;
    }else{
        if(which % 2 == 0)
                _ev.emplace_back(create_event( 
                    RoomEventType::Player1_Fatigue, 
                    EventVisibility::Public, 
                    -1, player.fatigue_damage));
            else
                _ev.emplace_back(create_event( 
                    RoomEventType::Player2_Fatigue, 
                    EventVisibility::Public,
                    -1, player.fatigue_damage));
        player.health -= player.fatigue_damage;
        player.fatigue_damage++;
        return false;
    }
}

std::shared_ptr<BattleRoom> RoomService::createRoom(const MatchInfo &match)
{
    if(match.players[1]==match.players[0])return nullptr;
    auto ret = std::make_shared<BattleRoom>();
    auto init_success = ret->init_with_match_info(match);
    std::lock_guard<std::mutex> guard(mutex_);
    if(playerRooms.find(match.players[0])!= playerRooms.end() || 
        playerRooms.find(match.players[1])!= playerRooms.end())return nullptr;
    if(init_success){
        rooms[room_id_counter] = ret;
        playerRooms[match.players[0]] = room_id_counter;
        playerRooms[match.players[1]] = room_id_counter;
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
    return it != playerRooms.end();
}

bool RoomService::removeRoom(int64_t roomId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = rooms.find(roomId);
    if(it!=rooms.end())
    {
        playerRooms.erase(it->second->getPlayer1Id());
        playerRooms.erase(it->second->getPlayer2Id());
        rooms.erase(it);
        return true;
    }
    return false;
}

static RoomService RoomServiceServer;

RoomService &RoomService::GetServer()
{
    return RoomServiceServer;
}
