#include "UserController.h"
#include "models/Users.h"
#include "models/Cards.h"
#include "models/PlayerCards.h"
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <array>
#include"session_check.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

using ResponseCallback = std::function<void(const drogon::HttpResponsePtr &)>;

namespace
{
struct TestCardSeed
{
    std::string cardKey;
    std::string name;
    std::string description;
    int manaCost;
    int attack;
    int health;
    std::string imageUrl;
};

const std::array<TestCardSeed, 30> kTestCardSeeds = {{
    {"test_card_01", "Test Card 1", "for test card 1", 1, 1, 1, "/test-cards/card-1.svg"},
    {"test_card_02", "Test Card 2", "for test card 2", 1, 1, 2, "/test-cards/card-2.svg"},
    {"test_card_03", "Test Card 3", "for test card 3", 1, 2, 1, "/test-cards/card-3.svg"},
    {"test_card_04", "Test Card 4", "for test card 4", 2, 2, 2, "/test-cards/card-4.svg"},
    {"test_card_05", "Test Card 5", "for test card 5", 2, 3, 1, "/test-cards/card-5.svg"},
    {"test_card_06", "Test Card 6", "for test card 6", 2, 1, 4, "/test-cards/card-6.svg"},
    {"test_card_07", "Test Card 7", "for test card 7", 3, 3, 2, "/test-cards/card-7.svg"},
    {"test_card_08", "Test Card 8", "for test card 8", 3, 2, 4, "/test-cards/card-8.svg"},
    {"test_card_09", "Test Card 9", "for test card 9", 3, 4, 2, "/test-cards/card-9.svg"},
    {"test_card_10", "Test Card 10", "for test card 10", 4, 4, 3, "/test-cards/card-10.svg"},
    {"test_card_11", "Test Card 11", "for test card 11", 4, 5, 2, "/test-cards/card-11.svg"},
    {"test_card_12", "Test Card 12", "for test card 12", 4, 2, 6, "/test-cards/card-12.svg"},
    {"test_card_13", "Test Card 13", "for test card 13", 5, 5, 4, "/test-cards/card-13.svg"},
    {"test_card_14", "Test Card 14", "for test card 14", 5, 6, 3, "/test-cards/card-14.svg"},
    {"test_card_15", "Test Card 15", "for test card 15", 5, 3, 7, "/test-cards/card-15.svg"},
    {"test_card_16", "Test Card 16", "for test card 16", 6, 6, 4, "/test-cards/card-16.svg"},
    {"test_card_17", "Test Card 17", "for test card 17", 6, 7, 3, "/test-cards/card-17.svg"},
    {"test_card_18", "Test Card 18", "for test card 18", 6, 4, 8, "/test-cards/card-18.svg"},
    {"test_card_19", "Test Card 19", "for test card 19", 7, 7, 5, "/test-cards/card-19.svg"},
    {"test_card_20", "Test Card 20", "for test card 20", 7, 8, 4, "/test-cards/card-20.svg"},
    {"test_card_21", "Test Card 21", "for test card 21", 7, 5, 9, "/test-cards/card-21.svg"},
    {"test_card_22", "Test Card 22", "for test card 22", 8, 8, 5, "/test-cards/card-22.svg"},
    {"test_card_23", "Test Card 23", "for test card 23", 8, 9, 4, "/test-cards/card-23.svg"},
    {"test_card_24", "Test Card 24", "for test card 24", 8, 6, 10, "/test-cards/card-24.svg"},
    {"test_card_25", "Test Card 25", "for test card 25", 9, 9, 6, "/test-cards/card-25.svg"},
    {"test_card_26", "Test Card 26", "for test card 26", 9, 10, 5, "/test-cards/card-26.svg"},
    {"test_card_27", "Test Card 27", "for test card 27", 9, 7, 11, "/test-cards/card-27.svg"},
    {"test_card_28", "Test Card 28", "for test card 28", 10, 10, 6, "/test-cards/card-28.svg"},
    {"test_card_29", "Test Card 29", "for test card 29", 10, 11, 5, "/test-cards/card-29.svg"},
    {"test_card_30", "Test Card 30", "for test card 30", 10, 8, 12, "/test-cards/card-30.svg"},
}};

void ensure_test_cards_and_grant_to_user(const drogon::orm::DbClientPtr &client, int64_t userId)
{
    auto trans = client->newTransaction(); // for test
    Mapper<Cards> cardMapper(trans); // for test
    Mapper<PlayerCards> playerCardMapper(trans); // for test

    for(const auto &seed : kTestCardSeeds)
    {
        auto cards = cardMapper.findBy(Criteria(Cards::Cols::_card_key, CompareOperator::EQ, seed.cardKey)); // for test
        int64_t cardId = 0;
        if(cards.empty())
        {
            Cards card; // for test
            card.setCardKey(seed.cardKey);
            card.setName(seed.name);
            card.setDescription(seed.description);
            card.setManaCost(seed.manaCost);
            card.setAttack(seed.attack);
            card.setHealth(seed.health);
            card.setCardType("minion");
            card.setRarity("common");
            card.setClassType("unused");
            card.setEffectJson("{}");
            card.setImageUrl(seed.imageUrl);
            card.setIsCollectible(true);
            cardMapper.insert(card);
            cardId = card.getValueOfId();
        }
        else
        {
            cardId = cards.front().getValueOfId();
        }

        auto playerCards = playerCardMapper.findBy(
            Criteria(PlayerCards::Cols::_user_id, CompareOperator::EQ, userId) &&
            Criteria(PlayerCards::Cols::_card_id, CompareOperator::EQ, cardId)); // for test
        if(playerCards.empty())
        {
            PlayerCards playerCard; // for test
            playerCard.setUserId(userId);
            playerCard.setCardId(cardId);
            playerCard.setQuantity(2);
            playerCardMapper.insert(playerCard);
        }
    }
}
}

void sendJsonResponse(const ResponseCallback& callback,
    drogon::HttpStatusCode code,
    const std::string &message)
{
    Json::Value json;
    json["state"] = code >= drogon::k400BadRequest ? "ERROR" : "SUCCESS";
    json["message"] = message;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(code);
    callback(resp);
}

auto makeDbExceptionHandler(ResponseCallback callback){
    return [callback](const drogon::orm::DrogonDbException &e)->void
    {
        sendJsonResponse(
            callback,
            drogon::k500InternalServerError,
            e.base().what());
    };
}

std::string sha256(
    const std::string& input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(
        reinterpret_cast<const unsigned char*>(input.c_str()),
        input.size(), hash);

    std::stringstream ss;

    for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        ss << std::hex
           << std::setw(2)
           << std::setfill('0')
           << static_cast<int>(hash[i]);
    }

    return ss.str();
}

void UserController::registerUser(
    const HttpRequestPtr &req,
    std::function<void(
        const HttpResponsePtr &)> &&callback)
{
    auto cb = std::move(callback);
    auto username = req->getParameter("username");
    auto password = req->getParameter("password");
    auto client = app().getDbClient("db");
    auto Pmapper = std::make_shared<Mapper<Users>>(client);

    if(username.empty() || password.empty())
    {
        sendJsonResponse(cb, k400BadRequest, "Need username and password!");
        return;
    }

    password = sha256(password);

    //Note findBy is async
    Pmapper->findBy(
        Criteria(Users::Cols::_username, CompareOperator::EQ, username),
        [cb, username, password, Pmapper] (std::vector<Users> users)->void
        {//callback for correct
            if(!users.empty())
            {
                sendJsonResponse(cb, k400BadRequest, "User exists!");
                return;
            }

            Users user;

            user.setUsername(username);
            user.setPasswordHash(password);//TODO check passwd

            Pmapper->insert(user,
                [cb, client](Users user)->void
                {
                    try
                    {
                        ensure_test_cards_and_grant_to_user(client, user.getValueOfId()); // for test
                    }
                    catch(const DrogonDbException &e)
                    {
                        sendJsonResponse(cb, k500InternalServerError, e.base().what());
                        return;
                    }
                    sendJsonResponse(cb, k200OK, "Success!");
                },
                makeDbExceptionHandler(cb));
        },
        makeDbExceptionHandler(cb));
}

void UserController::loginUser(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    {
        int64_t userid;
        switch (MySessionChecker::check_session(req, userid))
        {
        case MySessionChecker::ErrInvaildToken:
            req->session()->erase("user_id");
            req->session()->erase("req_token");
            break;
        case MySessionChecker::SessionOk:
            sendJsonResponse(callback, k409Conflict, "Error, logged in");
            return;
        }
    }
    auto username = req->getParameter("username");
    auto password = req->getParameter("password");
    auto cb = std::move(callback);

    if(username.empty() || password.empty())
    {
        sendJsonResponse(cb, k400BadRequest, "Need username and password!");
        return;
    }

    password = sha256(password);
    auto client = app().getDbClient("db");
    auto mapper = std::make_shared<Mapper<Users>>(client);

    mapper->findBy(
        Criteria(Users::Cols::_username, CompareOperator::EQ, username),

        [cb, password, req](std::vector<Users> users)->void
        {
            if(users.empty())
            {
                sendJsonResponse(cb,k401Unauthorized,"Invalid username");
                return;
            }

            const auto &user = users[0];

            if(user.getValueOfPasswordHash() != password)
            {
                sendJsonResponse(cb, k401Unauthorized, "Incorrect password");
                return;
            }

            if(MySessionChecker::write_session_info(req, user.getValueOfId()))
            sendJsonResponse(cb, k200OK, "login success");
            else sendJsonResponse(cb, k409Conflict, "cannot login");
        },
        makeDbExceptionHandler(cb));
}

void UserController::status(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    ResponseCallback cb = std::move(callback);

    int64_t userid;
    switch(MySessionChecker::check_session(req, userid)){
        case MySessionChecker::ErrNotLogin:
        sendJsonResponse(cb, k401Unauthorized, "Error! not logged in");
        return;
        case MySessionChecker::ErrInvaildToken:
        sendJsonResponse(cb, k401Unauthorized, "Error! invaild session token");
        return;
        case MySessionChecker::SessionOk:
        break;
    }

    auto client = app().getDbClient("db");
    auto mapper = std::make_shared<Mapper<Users>>(client);

    mapper->findBy(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userid),
        [cb](std::vector<Users> users)->void
        {
            if(users.empty()){
                sendJsonResponse(cb, k400BadRequest, "Session Error! User not exist!");
                return;
            }
            sendJsonResponse(cb, k200OK, "Logged in as " + users[0].getValueOfUsername());
        },
        makeDbExceptionHandler(cb));
}

void UserController::logout(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    int64_t userid;
    switch(MySessionChecker::check_session(req, userid)){
        case MySessionChecker::ErrNotLogin:
        sendJsonResponse(callback, k401Unauthorized, "Error! not logged in");
        return;
        case MySessionChecker::ErrInvaildToken:
        sendJsonResponse(callback, k401Unauthorized, "Error! invaild session token");
        return;
        case MySessionChecker::SessionOk:
        MySessionChecker::erase_session_info(req);
        sendJsonResponse(callback, k200OK, "Logout success");

    }
}
