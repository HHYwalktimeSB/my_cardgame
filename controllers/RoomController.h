#pragma once

#include <drogon/HttpController.h>
#include"RoomService.h"

using namespace drogon;

class RoomController : public drogon::HttpController<RoomController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RoomController::poll, "/battleroom/{1:roomid}/poll", Post);
    ADD_METHOD_TO(RoomController::stat, "/battleroom/{1:roomid}/stat", Get);
    ADD_METHOD_TO(RoomController::snapshot, "/battleroom/{1:roomid}/snapshot", Get);
    ADD_METHOD_TO(RoomController::operation, "/battleroom/{1:roomid}/operation", Post);
    ADD_METHOD_TO(RoomController::leave, "/battleroom/{1:roomid}/leave", Post);
    ADD_METHOD_TO(RoomController::get_current, "/battleroom/current", Get);
    METHOD_LIST_END

    void poll(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& roomid
    );
    void stat(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& roomid
    );
    void snapshot(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& roomid
    );
    void operation(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& roomid
    );
    void leave(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& roomid
    );
    void get_current(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback
    );
    protected:
    std::shared_ptr<BattleRoom> getroom_(int64_t roomid, int64_t userid, bool & success,
        const std::function<void(const HttpResponsePtr &)> &callback);
    int64_t str_roomid_to_int(const std::string& roomid, 
        const std::function<void(const HttpResponsePtr &)> &callback);
    void respond_w_error(const std::string& msg, HttpStatusCode code, const std::function<void(const HttpResponsePtr &)> &callback);
};
