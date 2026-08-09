#include "RoomController.h"
#include "session_check.h"

void RoomController::poll(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &str_roomid)
{
    int64_t userId;
    std::remove_reference_t<decltype(callback)> cb = std::move(callback);
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_w_error("not logged in", k401Unauthorized, cb);
        return;
    }
    bool success;
    int64_t roomId = this->str_roomid_to_int(str_roomid, cb);
    if(roomId < 0)return;
    auto room = getroom_(roomId, userId, success, cb);
    if(!success)return;
    auto args = req->getJsonObject();
    if(!args){
        respond_w_error("missing argumenrt", k400BadRequest, cb);
        return;
    }
    uint64_t seq = (*args)["sequence"].asInt64();
    {
        bool has_room_access = false;
        auto resp = HttpResponse::newHttpJsonResponse(
            room->getEventsAfterJson(seq, userId, has_room_access));
        if(!has_room_access){
            resp->setStatusCode(k401Unauthorized);
            cb(resp);
            return;
        }
        if(!(*resp->getJsonObject())["events"].empty()){
            resp->setStatusCode(k200OK);
            cb(resp);
            return;
        }
    }
    
    auto poll_register_res = RoomService::GetServer().registerPoll(roomId,userId,
    [userId, seq, cb, room]()->void{
        bool success;
        auto resp = HttpResponse::newHttpJsonResponse(
        room->getEventsAfterJson(seq, userId, success));
        if(success)resp->setStatusCode(k200OK);
        else resp->setStatusCode(k401Unauthorized);
        cb(resp);
    });

    if(poll_register_res.first){
        drogon::app().getLoop()->runAfter(25.0, [roomId, token = poll_register_res.second](){
            RoomService::GetServer().invokePollWithToken(roomId, token);
        });
    }
    else{
        respond_w_error("duplicate poll", k409Conflict, cb);
    }

}

void RoomController::stat(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &str_roomid)
{
    int64_t roomId = this->str_roomid_to_int(str_roomid, callback);
    if(roomId < 0)return;
    auto room = RoomService::GetServer().findRoom(roomId);
    if(room){
        Json::Value res;
        res["state"] = "SUCCESS";
        int64_t winner;
        auto state = room->getStatus_and_winnder(winner);
        switch (state)
        {
        case BattleRoom::RoomStatus::Finished:
        res["room_state"] = "finished";
        res["winner"] = winner;
        break;
        case BattleRoom::RoomStatus::Playing:
        res["room_state"] = "playing";
        break;
        case BattleRoom::RoomStatus::Preparing:
        res["room_state"] = "perparing";
        break;
        default:
        res["room_state"] = "unknow";
        break;
        }
        auto resp = HttpResponse::newHttpJsonResponse(res);
        callback(resp);
    }else respond_w_error("can't find room", k404NotFound, callback);
}

void RoomController::snapshot(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &str_roomid)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_w_error("not logged in", k401Unauthorized, callback);
        return;
    }
    bool success;
    int64_t roomId = this->str_roomid_to_int(str_roomid, callback);
    if(roomId < 0)return;
    auto room = getroom_(roomId, userId, success, callback);
    if(!success)return;
    auto resp = HttpResponse::newHttpJsonResponse(
        room->getSnapshotJson(userId, success));
    if(success)resp->setStatusCode(k200OK);
    else resp->setStatusCode(k401Unauthorized);
    callback(resp);
}

void RoomController::operation(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &str_roomid)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_w_error("not logged in", k401Unauthorized, callback);
        return;
    }
    bool success;
    int64_t roomId = this->str_roomid_to_int(str_roomid, callback);
    if(roomId < 0)return;
    auto room = getroom_(roomId, userId, success, callback);
    if(!success)return;
    auto args = req->getJsonObject();
    if(!args){
        respond_w_error("missing argumenrt", k400BadRequest, callback);
        return;
    }
    BattleRoom::Operation op;
    op.cardInstanceId = (*args)["card_instance"].asInt64();
    op.expectedVersion = (*args)["version"].asUInt64();
    op.requestId = (*args)["request_id"].asUInt64();
    op.targetId = (*args)["target"].asInt64();
    std::string type = (*args)["type"].asString();
    //for(auto&c : type)c = tolower(c);
    if(type == "attack")op.type = BattleRoom::OperationType::Attack;
    else if(type == "end turn")op.type = BattleRoom::OperationType::EndTurn;
    else if(type == "play card")op.type = BattleRoom::OperationType::PlayCard;
    else if(type == "surrender")op.type = BattleRoom::OperationType::Surrender;
    else{
        respond_w_error("invaild arguments", k400BadRequest,callback);
        return;
    }
    auto result = room->applyOperation(userId, op);
    Json::Value res;
    success = false;
    switch(result.error){
        case BattleRoom::ActionError::InsufficientMana:
        res["action_error"] = "InsufficientMana";
        break;
        case BattleRoom::ActionError::InvalidCard:
        res["action_error"] = "InvalidCard";
        break;
        case BattleRoom::ActionError::InvalidTarget:
        res["action_error"] = "InvalidTarget";
        break;
        case BattleRoom::ActionError::NotYourTurn:
        res["action_error"] = "NotYourTurn";
        break;
        case BattleRoom::ActionError::PlayerNotInRoom:
        res["action_error"] = "PlayerNotInRoom";
        break;
        case BattleRoom::ActionError::RoomFinished:
        res["action_error"] = "RoomFinished";
        break;
        case BattleRoom::ActionError::StaleVersion:
        res["action_error"] = "StaleVersion";
        break;
        default:
        success = true;
    }
    if(success){
        res["state"] = "SUCCESS";
        res["version"] = result.version;
        res["events"] = BattleRoom::eventListToJson(result.generatedEvents);
        if(room->isFinished())
            RoomService::GetServer().scheduleFinishedRoomCleanup(roomId);
        RoomService::GetServer().invokePolls(roomId);
    }else res["state"] = "FAIL";
    auto resp = HttpResponse::newHttpJsonResponse(res);
    callback(resp);
}

void RoomController::leave(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &str_roomid)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_w_error("not logged in", k401Unauthorized, callback);
        return;
    }
    bool success;
    int64_t roomId = this->str_roomid_to_int(str_roomid, callback);
    if(roomId < 0)return;
    auto room = getroom_(roomId, userId, success, callback);
    if(!success)return;
    if(!room->isFinished()){
        respond_w_error("room not finished", k409Conflict, callback);
        return;
    }
    if(!RoomService::GetServer().playerLeaveRoom(roomId, userId)){
        respond_w_error("leave room failed", k400BadRequest, callback);
        return;
    }
    Json::Value res;
    res["state"] = "SUCCESS";
    auto resp = HttpResponse::newHttpJsonResponse(res);
    callback(resp);
}

void RoomController::get_current(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_w_error("not logged in", k401Unauthorized, callback);
        return;
    }
    Json::Value res;
    res["state"] = "SUCCESS";
    auto room = RoomService::GetServer().findRoomByPlayer(userId);
    if(room){
        auto xx = room->quickStateGetting(userId);
        if(!xx.function_success){
            res["in_room"] = false;
            auto resp = HttpResponse::newHttpJsonResponse(res);
            callback(resp);
            return;
        }
        res["in_room"] = true;
        res["room_id"] = xx.roomId;
        res["match_id"] = xx.matchId;
        res["opponent_id"] = xx.opponentId;
        switch (xx.state)
        {
        case BattleRoom::RoomStatus::Playing:
            res["room_state"] = "playing";
            break;
        case BattleRoom::RoomStatus::Preparing:
            res["room_state"] = "perparing";
            break;
        case BattleRoom::RoomStatus::Finished: 
            res["room_state"] = "finished";
            break;
        default:
            res["room_state"] = "unknow";
            break;
        }
    }else res["in_room"] = false;
    auto resp = HttpResponse::newHttpJsonResponse(res);
    callback(resp);
}

std::shared_ptr<BattleRoom> RoomController::getroom_(int64_t roomid, int64_t userid, bool &success, const std::function<void(const HttpResponsePtr &)> &callback)
{
    auto room = RoomService::GetServer().findRoom(roomid);
    if(!room){
        success = false;
        respond_w_error("can't find room", k404NotFound, callback);
        return nullptr;
    }
    if(!room->containPlayer(userid)){
        success = false;
        respond_w_error("player not in room", k401Unauthorized, callback);
        return nullptr;
    }
    success = true;
    return room;
}

int64_t RoomController::str_roomid_to_int(const std::string &roomid, const std::function<void(const HttpResponsePtr &)> &callback)
{
    int64_t id;
    try{
        id = std::stol(roomid);
    }
    catch(std::invalid_argument){
        id = -1;
    }
    if(id<0)
        respond_w_error("invaild roomid", k400BadRequest, callback);
    return id;
}

void RoomController::respond_w_error(const std::string &msg, HttpStatusCode code, const std::function<void(const HttpResponsePtr &)> &callback)
{
    Json::Value res;
    res["state"] = "ERROR";
    res["message"] = msg;
    auto resp = HttpResponse::newHttpJsonResponse(res);
    resp->setStatusCode(code);
    callback(resp);
}
