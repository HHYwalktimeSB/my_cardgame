#ifndef BATTLE_WEBSOCKET_PROTOCOL_H_
#define BATTLE_WEBSOCKET_PROTOCOL_H_

#include <cstddef>

namespace cardgame::websocket_protocol
{

enum class ClientMessageType : int
{
    SyncEvents = 0,
    ApplyOperation = 1,
};

enum class ServerMessageType : int
{
    OperationResult = 2,
};

enum class WireTargetType : int
{
    Minion = 0,
    Hero = 1,
};

constexpr size_t kSyncMessageFieldCount = 2;
constexpr size_t kOperationMessageFieldCount = 7;
constexpr int kCompactEventFormatVersion = 1;

constexpr unsigned int kMessageTypeField = 0;
constexpr unsigned int kRequestIdField = 1;
constexpr unsigned int kExpectedVersionField = 2;
constexpr unsigned int kOperationTypeField = 3;
constexpr unsigned int kCardInstanceField = 4;
constexpr unsigned int kTargetIdField = 5;
constexpr unsigned int kTargetTypeField = 6;

}  // namespace cardgame::websocket_protocol

#endif
