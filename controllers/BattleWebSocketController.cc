#include "BattleWebSocketController.h"

#include "BattleWebSocketProtocol.h"
#include "RoomService.h"
#include "session_check.h"

#include <jsoncpp/json/json.h>
#include <sstream>

namespace
{

using cardgame::websocket_protocol::ClientMessageType;
using cardgame::websocket_protocol::ServerMessageType;
using cardgame::websocket_protocol::WireTargetType;

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
    response += '[';
    response += std::to_string(static_cast<int>(
        ServerMessageType::OperationResult));
    response += ',';
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
       !root.isArray() || root.empty() ||
       !root[cardgame::websocket_protocol::kMessageTypeField].isIntegral())
    {
        connection->shutdown(drogon::CloseCode::kWrongMessageContent, "invalid message");
        return;
    }

    const int messageType =
        root[cardgame::websocket_protocol::kMessageTypeField].asInt();
    if(messageType == static_cast<int>(ClientMessageType::SyncEvents) &&
       root.size() == cardgame::websocket_protocol::kSyncMessageFieldCount &&
       root[cardgame::websocket_protocol::kRequestIdField].isUInt64())
    {
        RoomService::GetServer().syncWebSocket(
            connection,
            root[cardgame::websocket_protocol::kRequestIdField].asUInt64());
        return;
    }
    if(messageType != static_cast<int>(ClientMessageType::ApplyOperation) ||
       root.size() != cardgame::websocket_protocol::kOperationMessageFieldCount ||
       !root[cardgame::websocket_protocol::kRequestIdField].isUInt64() ||
       !root[cardgame::websocket_protocol::kExpectedVersionField].isUInt64() ||
       !root[cardgame::websocket_protocol::kOperationTypeField].isInt() ||
       !root[cardgame::websocket_protocol::kCardInstanceField].isInt64() ||
       !root[cardgame::websocket_protocol::kTargetIdField].isInt64() ||
       !root[cardgame::websocket_protocol::kTargetTypeField].isInt())
    {
        connection->shutdown(drogon::CloseCode::kWrongMessageContent, "invalid message");
        return;
    }

    const int operationType =
        root[cardgame::websocket_protocol::kOperationTypeField].asInt();
    const int targetType =
        root[cardgame::websocket_protocol::kTargetTypeField].asInt();
    if(operationType < static_cast<int>(BattleRoom::OperationType::PlayCard) ||
       operationType > static_cast<int>(BattleRoom::OperationType::Surrender) ||
       targetType < static_cast<int>(WireTargetType::Minion) ||
       targetType > static_cast<int>(WireTargetType::Hero))
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
    operation.requestId =
        root[cardgame::websocket_protocol::kRequestIdField].asUInt64();
    operation.expectedVersion =
        root[cardgame::websocket_protocol::kExpectedVersionField].asUInt64();
    operation.type = static_cast<BattleRoom::OperationType>(operationType);
    operation.cardInstanceId =
        root[cardgame::websocket_protocol::kCardInstanceField].asInt64();
    operation.targetId =
        root[cardgame::websocket_protocol::kTargetIdField].asInt64();
    operation.targetType = static_cast<BattleRoom::TargetType>(targetType);

    auto result = room->applyOperation(userId, operation);
    if(result.error != BattleRoom::ActionError::None)
    {
        send_operation_result(connection, operation.requestId, result);
        return;
    }

    if(room->isFinished())service.scheduleFinishedRoomCleanup(roomId);
    if(!service.publishWebSocketEvents(
           roomId,
           result.publishEvents,
           connection,
           operation.requestId,
           result.version))
        send_operation_result(connection, operation.requestId, result);
}

void BattleWebSocketController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &connection)
{
    RoomService::GetServer().unregisterWebSocket(connection);
}
