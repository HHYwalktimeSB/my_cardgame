#include <drogon/drogon.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

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
    //drogon::app().loadConfigFile("../config.yaml");
    drogon::app().run();
    return 0;
}
