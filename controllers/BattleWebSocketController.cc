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

void send_operation_result(
    const drogon::WebSocketConnectionPtr &connection,
    uint64_t requestId,
    const BattleRoom::ActionResult &result)
{
    std::string response;
    response.reserve(64);
    response += "[2,";
    response += std::to_string(requestId);
    response += ',';
    response += std::to_string(static_cast<int>(result.error));
    response += ',';
    response += std::to_string(result.version);
    response += ']';
    connection->send(std::move(response));
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
       !root.isArray() || root.empty() || !root[0].isIntegral())
    {
        connection->shutdown(drogon::CloseCode::kWrongMessageContent, "invalid message");
        return;
    }

    const int messageType = root[0].asInt();
    if(messageType == 0 && root.size() == 2 && root[1].isUInt64())
    {
        RoomService::GetServer().syncWebSocket(connection, root[1].asUInt64());
        return;
    }
    if(messageType != 1 || root.size() != 7 ||
       !root[1].isUInt64() || !root[2].isUInt64() ||
       !root[3].isInt() || !root[4].isInt64() ||
       !root[5].isInt64() || !root[6].isInt())
    {
        connection->shutdown(drogon::CloseCode::kWrongMessageContent, "invalid message");
        return;
    }

    const int operationType = root[3].asInt();
    const int targetType = root[6].asInt();
    if(operationType < 0 || operationType > 3 ||
       targetType < 0 || targetType > 1)
    {
        connection->shutdown(drogon::CloseCode::kWrongMessageContent, "invalid operation");
        return;
    }

    int64_t roomId = -1;
    int64_t userId = -1;
    std::shared_ptr<BattleRoom> room;
    auto &service = RoomService::GetServer();
    if(!service.getWebSocketRoom(connection, roomId, userId, room))
    {
        connection->shutdown(drogon::CloseCode::kViolation, "connection not registered");
        return;
    }

    BattleRoom::Operation operation;
    operation.requestId = root[1].asUInt64();
    operation.expectedVersion = root[2].asUInt64();
    operation.type = static_cast<BattleRoom::OperationType>(operationType);
    operation.cardInstanceId = root[4].asInt64();
    operation.targetId = root[5].asInt64();
    operation.targetType = static_cast<BattleRoom::TargetType>(targetType);

    auto result = room->applyOperation(userId, operation);
    send_operation_result(connection, operation.requestId, result);
    if(result.error != BattleRoom::ActionError::None)return;

    auto serializedEvents = RoomService::serializeEventArray(
        result.generatedEvents);
    if(room->isFinished())service.scheduleFinishedRoomCleanup(roomId);
    service.publishWebSocketEvents(
        roomId, result.generatedEvents, serializedEvents);
}

void BattleWebSocketController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &connection)
{
    RoomService::GetServer().unregisterWebSocket(connection);
}
