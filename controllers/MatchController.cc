#include "MatchController.h"
#include "RoomService.h"
#include "session_check.h"
#include<models/Decks.h>

static MatchmakingService service;
using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

static void ErrorResponseWithJson(const std::string& err, const std::string& msg,
    HttpStatusCode code, const std::function<void(const HttpResponsePtr &)> &callback){
Json::Value error;
    error["state"] = "ERROR";
    error["error"] = err;
    error["message"] = msg;
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(code);
    callback(resp);
}

void MatchController::poll(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback
)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        ErrorResponseWithJson("not logged in", "user need to logged in to call the api", k401Unauthorized, callback);
        return;
    }
    
    auto loop = app().getLoop();
    size_t token;
    int64_t userid = userId;
    auto res = service.RegisterPoll(userid, callback);

    if(res.state==MatchmakingService::PollResultState::Added)
        loop->runAfter(
            25.0,
        [userid, token = res.token]()->void
        {
            bool is_matched;
            Match match;
            auto cb = service.queryMatch(userid, token, is_matched, match);
            if(cb){
                Json::Value json;
                if (is_matched)
                {
                    json["status"] = "MATCHED";
                    json["match_id"] = static_cast<Json::Int64>(match.matchId);
                    json["opponent_id"] = static_cast<Json::Int64>(match.opponentId);
                    json["room_id"] = static_cast<Json::Int64>(match.roomid);
                }
                else
                {
                    json["status"] = "WAITING";
                }
                auto resp = HttpResponse::newHttpJsonResponse(json);
                cb->operator()(resp);
            }
    });
    else if(res.state==MatchmakingService::PollResultState::Duplicate){
        ErrorResponseWithJson(
            "duplicate poll",
            "another poll request is already pending",
            k409Conflict, callback);
    }else if(res.state==MatchmakingService::PollResultState::Matched){
        Json::Value json;
        json["status"] = "MATCHED";
        json["match_id"] = static_cast<Json::Int64>(res.match.matchId);
        json["opponent_id"] = static_cast<Json::Int64>(res.match.opponentId);
        json["room_id"] = static_cast<Json::Int64>(res.match.roomid);
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    }else{
        ErrorResponseWithJson(
            "not in matching queue",
            "user is not in the matching queue, call /join before /poll",
            k400BadRequest, callback);
    }
}

void MatchController::join(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    int64_t userid;
    auto deckid = req->getParameter("deckid");
    if(MySessionChecker::check_session(req, userid) != MySessionChecker::SessionOk)
    {
        ErrorResponseWithJson("not logged in", "user need to logged in to call the api", k401Unauthorized, callback);
        return;
    }
    if(deckid.empty()){
        ErrorResponseWithJson("parameter error", "need a deck id", k400BadRequest, callback);
        return;
    }
    int state;
    int64_t deckidi;
    {
        size_t parsed = 0;
        try{
            deckidi = std::stoll(deckid, &parsed);
        } catch (const std::invalid_argument &) {
            deckidi = -1;
        }catch (const std::out_of_range &){
            deckidi = -1;
        }
        if(parsed != deckid.size() || deckidi < 0)
        {   
            ErrorResponseWithJson("invalid argument", "invalid deck id", k400BadRequest, callback);
            return;
        }
        try{
        auto dbClient = app().getDbClient("db");
    
        Mapper<Decks> deckMapper(dbClient);
        auto deck = deckMapper.findBy(Criteria(Decks::Cols::_id, CompareOperator::EQ, deckidi));
        if(deck.empty()||deck[0].getValueOfUserId() != userid){
            ErrorResponseWithJson("deckid", "invaild deckid", k400BadRequest, callback);
            return;
        }
        if(!deck[0].getValueOfIsActive()){
            ErrorResponseWithJson("inactive deck", "the deck is incomplete or contains invaild cards",
                k400BadRequest, callback);
            return;
        }
    }catch(const DrogonDbException &e){
        ErrorResponseWithJson("database error",e.base().what(),k500InternalServerError, callback);
        return;
    }
    }
    Match m = service.joinMatch(userid, deckidi, state);
    Json::Value json;
    HttpStatusCode resp_code = k200OK;
    if(state == MatchmakingService::Matched){
        json["status"] = "MATCHED";
        json["match_id"] = static_cast<Json::Int64>(m.matchId);
        json["opponent_id"] = static_cast<Json::Int64>(m.opponentId);
        json["room_id"] = static_cast<Json::Int64>(m.roomid);
        auto opcallback = service.takeCallback(m.opponentId);
        if(opcallback){
          Json::Value opponentJson;
          opponentJson["status"] = "MATCHED";
          opponentJson["match_id"] = static_cast<Json::Int64>(m.matchId);
          opponentJson["opponent_id"] = static_cast<Json::Int64>(userid);
          opponentJson["room_id"] = static_cast<Json::Int64>(m.roomid);
          auto opponentResp =
              HttpResponse::newHttpJsonResponse(opponentJson);
            resp_code = k200OK;
        (*opcallback)(opponentResp);
      }
    }else if(state == MatchmakingService::E_cannot_create_room){
        json["status"] = "FAIL";
        json["message"] = "battle room creation failed";
        auto opcallback = service.takeCallback(m.opponentId);
        if(opcallback){
          Json::Value opponentJson;
          opponentJson["status"] = "FAIL";
          opponentJson["message"] = "battle room creation failed";
          auto opponentResp = HttpResponse::newHttpJsonResponse(opponentJson);
          opponentResp ->setStatusCode(k409Conflict);
          resp_code = k409Conflict;
        (*opcallback)(opponentResp);
        }
    }else if(state == MatchmakingService::E_in_queue){
        json["status"] = "FAIL";
        json["message"] = "already in waiting queue";
        resp_code = k409Conflict;
    }else if(state == MatchmakingService::Waiting){
        json["status"] = "WAITING";
        resp_code = k200OK;
    }else if(state == MatchmakingService::E_in_match){
        json["status"] = "FAIL";
        json["message"] = "already in a match";
        auto room = RoomService::GetServer().findRoomByPlayer(userid);
        if(room)json["room_id"] = room->getRoomId();
        resp_code = k409Conflict;
    }
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(resp_code);
    callback(resp);
}

void MatchController::cancel(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    int64_t userid;
    if(MySessionChecker::check_session(req, userid) != MySessionChecker::SessionOk)
    {
        ErrorResponseWithJson("not logged in", "user need to logged in to call the api", k401Unauthorized, callback);
        return;
    }
    Json::Value json;
    bool cancel_success;
    auto cb = service.cancelMatch(userid, cancel_success);
    if(cancel_success)
    {
        json["status"] = "SUCCESS";
        if(cb){
            Json::Value json_poll;
            json_poll["status"] = "CANCELLED";
            (*cb)( HttpResponse::newHttpJsonResponse(json_poll));
        }
    }else{
        json["status"] = "FAIL";
        json["message"] = "cannot cancel because player not in queue";
    }
    auto resp = HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

bool MatchController::remove_match(int64_t userid)
{
    return service.remove_match(userid);
}

MatchmakingService::MatchmakingService()
{
    callback_id_counter = 0;
    this->player_generation_counter = 0;
    this->match_id_counter = 1;
}

Match MatchmakingService::joinMatch(int64_t userId, int64_t deckId, int &state)
{
    bool is_matched = false;
    Match ret = {0};
    if(RoomService::GetServer().is_player_in_room(userId))
        {
            state = E_in_match;
            return ret;
        }
    std::lock_guard<std::mutex> guard(lock);
    if(waitingPlayers.find(userId)!=waitingPlayers.end()){
        state = E_in_queue;
        return ret;
    }
    if(result.find(userId)!=result.end()){
        auto it = result.find(userId);
        int64_t usr2 = it->second.opponentId;
        int64_t matchid = it->second.matchId;
        result.erase(it);
        it = result.find(usr2);
        if(it != result.end() && it->second.matchId == matchid)
            result.erase(it);
    }

    while(!waitingQueue.empty()){
        auto opponent = waitingQueue.front();
        auto it = waitingPlayers.find(opponent.userId);
        if(it==waitingPlayers.end() || it->second != opponent.generation)
        {
            waitingQueue.pop();
            continue;//inactive player
        }
        //TODO: create Match in Database
        int64_t matchId = match_id_counter++;//for test the code

        ret.matchId = matchId;
        ret.opponentId = opponent.userId;
        Match smatch2 = {
            matchId,
            userId
        };
        {
            auto it = result.insert(std::make_pair(userId, ret)).first;
            is_matched = result.insert(std::make_pair(opponent.userId, smatch2 )).second;
            if(!is_matched){
                result.erase(it);
                waitingQueue.pop();
                waitingPlayers.erase(opponent.userId);
                continue;
            }
        }
        
        
        if(is_matched){
            waitingQueue.pop();
            waitingPlayers.erase(opponent.userId);
            MatchInfo roomcreateinfo;
            roomcreateinfo.matchId = matchId;
            roomcreateinfo.players[0] = userId;
            roomcreateinfo.decks[0] = deckId;
            roomcreateinfo.players[1] = opponent.userId;
            roomcreateinfo.decks[1] = opponent.deckId;
            auto roomhandle = RoomService::GetServer().createRoom(roomcreateinfo);
            if(roomhandle){
                ret.roomid = roomhandle->getRoomId();
                result[opponent.userId].roomid = ret.roomid;
                result[userId].roomid = ret.roomid;
            }
            else{
                result.erase(userId);
                result.erase(opponent.userId);
                state = E_cannot_create_room;
                return ret;
            }
        }
        break;//execute once
    }
    if(is_matched){
        state = Matched;
    }else{
        state = Waiting;
        waitingPlayers.insert(std::make_pair (userId, player_generation_counter));
        waitingQueue.push({userId, deckId, player_generation_counter});
        ++player_generation_counter;
    }
    return ret;
}

std::optional<MatchmakingService::ResponseCallback> MatchmakingService::cancelMatch(int64_t userId, bool& r)
{
    std::lock_guard<std::mutex> guard(lock);
    r = waitingPlayers.find(userId)!=waitingPlayers.end();
    if(r)waitingPlayers.erase(userId);
    else return std::nullopt;
    auto it = callbacks.find(userId);
    if(it!=callbacks.end()){
        auto cb = std::move(it->second.cb);
        callbacks.erase(it);
        return cb;
    }
    return std::nullopt;
}

std::optional<MatchmakingService::ResponseCallback> MatchmakingService::queryMatch(int64_t userId, size_t token, bool& is_matched, Match& match)
{
    bool r;
    std::lock_guard<std::mutex> guard(lock);
    auto it = result.find(userId);
    is_matched = it!=result.end();
    if(is_matched)
        match = it->second;
    auto cbit = callbacks.find(userId);
    if(cbit!=callbacks.end()&& cbit->second.id == token){
        auto cb = std::move(cbit->second.cb);
        callbacks.erase(cbit);
        return cb;
    }
    return std::nullopt;
}

std::optional<MatchmakingService::ResponseCallback> MatchmakingService::takeCallback(int64_t userid)
{
    std::lock_guard<std::mutex> guard(lock);
    auto it = callbacks.find(userid);
    if(it!=callbacks.end()){
        auto cb = std::move(it->second.cb);
        callbacks.erase(it);
        return cb;
    }
    return std::nullopt;
}

bool MatchmakingService::remove_match(int64_t userid)
{
    bool ret = false;
    std::lock_guard<std::mutex> guard(lock);
    auto it = result.find(userid);
    if(it != result.end()){
        int64_t usr2 = it->second.opponentId;
        int64_t matchid = it->second.matchId;
        result.erase(it);
        ret = true;
        it = result.find(usr2);
        if(it != result.end() && it->second.matchId == matchid){
            result.erase(it);
        }
    }
    return ret;
}

MatchmakingService::PollRegisterResult MatchmakingService::RegisterPoll(int64_t userid, const ResponseCallback &cb)
{
    std::lock_guard<std::mutex> guard(lock);
    auto matchIt = result.find(userid);
    if (matchIt != result.end())
        return {PollResultState::Matched, 0, matchIt->second};
    if(waitingPlayers.find(userid)==waitingPlayers.end())
        return {PollResultState::NotJoined, 0, {}};//not in queue
    if (callbacks.find(userid) != callbacks.end())
          return {PollResultState::Duplicate, 0, {}};

      size_t token = callback_id_counter++;
      callbacks.emplace(userid, callback_struct(token, cb));

      return {PollResultState::Added, token, {}};
}

bool MatchmakingService::is_player_in_match(int64_t userid)
{
    std::lock_guard<std::mutex> guard(service.lock);
    auto matchIt = service.result.find(userid);
    if (matchIt != service.result.end())return true;
    if(service.waitingPlayers.find(userid)!=service.waitingPlayers.end())
        return true;
    return false;
}
