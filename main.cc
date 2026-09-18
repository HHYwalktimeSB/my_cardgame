#include <drogon/drogon.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{

size_t sendQueueLimit()
{
    constexpr size_t defaultLimit = 256 * 1024;
    const char *value = std::getenv("CARD_GAME_SEND_QUEUE_LIMIT_BYTES");
    if(!value)return defaultLimit;
    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if(errno != 0 || end == value || *end != '\0' || parsed == 0 ||
       parsed > std::numeric_limits<size_t>::max())
    {
        std::cerr << "invalid CARD_GAME_SEND_QUEUE_LIMIT_BYTES\n";
        std::exit(1);
    }
    return static_cast<size_t>(parsed);
}

}

int main() {
    const char *addressValue = std::getenv("CARD_GAME_LISTEN_ADDRESS");
    const char *portValue = std::getenv("CARD_GAME_LISTEN_PORT");
    const char *configValue = std::getenv("CARD_GAME_CONFIG");
    const std::string address = addressValue ? addressValue : "0.0.0.0";
    const std::string configPath = configValue ? configValue : "../config.json";
    int port = 5555;
    if(portValue)
    {
        char *end = nullptr;
        errno = 0;
        const long parsed = std::strtol(portValue, &end, 10);
        if(errno != 0 || end == portValue || *end != '\0' ||
           parsed <= 0 || parsed > 65535)
        {
            std::cerr << "invalid CARD_GAME_LISTEN_PORT\n";
            return 1;
        }
        port = static_cast<int>(parsed);
    }

    drogon::app().addListener(address, port);
    drogon::app().loadConfigFile(configPath);
    const size_t queueLimit = sendQueueLimit();
    drogon::app().setConnectionCallback(
        [queueLimit](const trantor::TcpConnectionPtr &connection) {
            if(!connection->connected())return;
            connection->setHighWaterMarkCallback(
                [](const trantor::TcpConnectionPtr &slowConnection,
                   size_t pendingBytes) {
                    LOG_WARN << "closing slow client with " << pendingBytes
                             << " queued response bytes";
                    slowConnection->forceClose();
                },
                queueLimit);
        });
    //drogon::app().loadConfigFile("../config.yaml");
    drogon::app().run();
    return 0;
}
