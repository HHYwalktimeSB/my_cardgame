#pragma once

#include <drogon/WebSocketController.h>

class BattleWebSocketController
    : public drogon::WebSocketController<BattleWebSocketController>
{
public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/battleroom/ws");
    WS_PATH_LIST_END

    void handleNewMessage(
        const drogon::WebSocketConnectionPtr &connection,
        std::string &&message,
        const drogon::WebSocketMessageType &type) override;
    void handleNewConnection(
        const drogon::HttpRequestPtr &request,
        const drogon::WebSocketConnectionPtr &connection) override;
    void handleConnectionClosed(
        const drogon::WebSocketConnectionPtr &connection) override;
};
