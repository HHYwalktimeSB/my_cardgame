#include "BattleWebSocketController.h"

#include "RoomService.h"
#include "session_check.h"

#include <jsoncpp/json/json.h>
#include <sstream>

namespace
{

bool parse_uint64(const std::string &text, uint64_t &value)
{
    if(text.empty())return false;
    size_t parsed = 0;
    try
    {
        value = std::stoull(text, &parsed);
    }
    catch(const std::exception &)
    {
        return false;
    }
    return parsed == text.size();
}

}

void BattleWebSocketController::handleNewConnection(
    const drogon::HttpRequestPtr &request,
    const drogon::WebSocketConnectionPtr &connection)
{
    int64_t userId = -1;
    if(MySessionChecker::check_session(request, userId) !=
       MySessionChecker::SessionOk)
    {
        connection->shutdown(drogon::CloseCode::kViolation, "not logged in");
        return;
    }

    uint64_t roomId = 0;
    uint64_t sequence = 0;
    if(!parse_uint64(request->getParameter("room_id"), roomId) ||
       (!request->getParameter("sequence").empty() &&
        !parse_uint64(request->getParameter("sequence"), sequence)))
    {
        connection->shutdown(drogon::CloseCode::kViolation, "invalid arguments");
        return;
    }

    auto room = RoomService::GetServer().findRoom(static_cast<int64_t>(roomId));
    if(!room || !room->containPlayer(userId))
    {
        connection->shutdown(drogon::CloseCode::kViolation, "player not in room");
        return;
    }

    RoomService::GetServer().registerWebSocket(
        static_cast<int64_t>(roomId), userId, sequence, connection);
}

void BattleWebSocketController::handleNewMessage(
    const drogon::WebSocketConnectionPtr &connection,
    std::string &&message,
    const drogon::WebSocketMessageType &type)
{
    if(type != drogon::WebSocketMessageType::Text)return;
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(message);
    if(!Json::parseFromStream(builder, stream, &root, &errors) ||
       root["type"].asString() != "sync" || !root["sequence"].isUInt64())
    {
        connection->shutdown(drogon::CloseCode::kWrongMessageContent, "invalid message");
        return;
    }
    RoomService::GetServer().syncWebSocket(
        connection, root["sequence"].asUInt64());
}

void BattleWebSocketController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &connection)
{
    RoomService::GetServer().unregisterWebSocket(connection);
}
