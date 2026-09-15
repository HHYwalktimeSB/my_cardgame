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
    std::string effectJson;
};

const std::array<TestCardSeed, 30> kTestCardSeeds = {{
    {"test_card_01", "Ember Scout", "Battlecry: Deal 2 damage to a selected character.", 2, 1, 2, "/test-cards/card-1.svg", R"({"battlecry":[{"type":"damage","target":"selected","value":2}]})"},
    {"test_card_02", "Field Medic", "Battlecry: Restore 2 health to your hero.", 2, 2, 2, "/test-cards/card-2.svg", R"({"battlecry":[{"type":"heal","target":"friendly_hero","value":2}]})"},
    {"test_card_03", "Growing Squire", "Battlecry: Gain +1/+1.", 2, 1, 2, "/test-cards/card-3.svg", R"({"battlecry":[{"type":"buff","target":"self","value":1}]})"},
    {"test_card_04", "Volatile Beetle", "Deathrattle: Deal 2 damage to the enemy hero.", 2, 2, 1, "/test-cards/card-4.svg", R"({"deathrattle":[{"type":"damage","target":"enemy_hero","value":2}]})"},
    {"test_card_05", "Kindly Guardian", "Deathrattle: Restore 2 health to your hero.", 3, 2, 4, "/test-cards/card-5.svg", R"({"deathrattle":[{"type":"heal","target":"friendly_hero","value":2}]})"},
    {"test_card_06", "Ashen Raider", "Battlecry and Deathrattle: Deal 1 damage to the enemy hero.", 4, 3, 3, "/test-cards/card-6.svg", R"({"battlecry":[{"type":"damage","target":"enemy_hero","value":1}],"deathrattle":[{"type":"damage","target":"enemy_hero","value":1}]})"},
    {"test_card_07", "Training Brute", "Battlecry: Gain +2/+2.", 5, 3, 3, "/test-cards/card-7.svg", R"({"battlecry":[{"type":"buff","target":"self","value":2}]})"},
    {"test_card_08", "Dawn Phoenix", "Battlecry: Deal 2 damage. Deathrattle: Restore 2 health.", 6, 5, 4, "/test-cards/card-8.svg", R"({"battlecry":[{"type":"damage","target":"enemy_hero","value":2}],"deathrattle":[{"type":"heal","target":"friendly_hero","value":2}]})"},
    {"test_card_09", "Doomed Herald", "Deathrattle: Deal 3 damage to the enemy hero.", 5, 4, 4, "/test-cards/card-9.svg", R"({"deathrattle":[{"type":"damage","target":"enemy_hero","value":3}]})"},
    {"test_card_10", "Last Lightkeeper", "Battlecry: Restore 3 health. Deathrattle: Deal 1 damage.", 6, 5, 6, "/test-cards/card-10.svg", R"({"battlecry":[{"type":"heal","target":"friendly_hero","value":3}],"deathrattle":[{"type":"damage","target":"enemy_hero","value":1}]})"},
    {"test_card_11", "Test Recruit", "A 1-cost 1/2 minion with no special effect.", 1, 1, 2, "/test-cards/card-11.svg", "{}"},
    {"test_card_12", "Test Duelist", "A 1-cost 2/1 minion with no special effect.", 1, 2, 1, "/test-cards/card-12.svg", "{}"},
    {"test_card_13", "Test Guard", "A 2-cost 2/3 minion with no special effect.", 2, 2, 3, "/test-cards/card-13.svg", "{}"},
    {"test_card_14", "Test Brawler", "A 2-cost 3/2 minion with no special effect.", 2, 3, 2, "/test-cards/card-14.svg", "{}"},
    {"test_card_15", "Test Soldier", "A 3-cost 3/4 minion with no special effect.", 3, 3, 4, "/test-cards/card-15.svg", "{}"},
    {"test_card_16", "Test Striker", "A 3-cost 4/3 minion with no special effect.", 3, 4, 3, "/test-cards/card-16.svg", "{}"},
    {"test_card_17", "Test Defender", "A 4-cost 4/5 minion with no special effect.", 4, 4, 5, "/test-cards/card-17.svg", "{}"},
    {"test_card_18", "Test Charger", "A 4-cost 5/4 minion with no special effect.", 4, 5, 4, "/test-cards/card-18.svg", "{}"},
    {"test_card_19", "Test Veteran", "A 5-cost 5/6 minion with no special effect.", 5, 5, 6, "/test-cards/card-19.svg", "{}"},
    {"test_card_20", "Test Ravager", "A 5-cost 6/5 minion with no special effect.", 5, 6, 5, "/test-cards/card-20.svg", "{}"},
    {"test_card_21", "Test Champion", "A 6-cost 6/7 minion with no special effect.", 6, 6, 7, "/test-cards/card-21.svg", "{}"},
    {"test_card_22", "Test Breaker", "A 6-cost 7/6 minion with no special effect.", 6, 7, 6, "/test-cards/card-22.svg", "{}"},
    {"test_card_23", "Test Colossus", "A 7-cost 7/8 minion with no special effect.", 7, 7, 8, "/test-cards/card-23.svg", "{}"},
    {"test_card_24", "Test Crusher", "A 7-cost 8/7 minion with no special effect.", 7, 8, 7, "/test-cards/card-24.svg", "{}"},
    {"test_card_25", "Test Ancient", "An 8-cost 8/9 minion with no special effect.", 8, 8, 9, "/test-cards/card-25.svg", "{}"},
    {"test_card_26", "Test Behemoth", "An 8-cost 9/8 minion with no special effect.", 8, 9, 8, "/test-cards/card-26.svg", "{}"},
    {"test_card_27", "Test Titan", "A 9-cost 9/10 minion with no special effect.", 9, 9, 10, "/test-cards/card-27.svg", "{}"},
    {"test_card_28", "Test Destroyer", "A 9-cost 10/9 minion with no special effect.", 9, 10, 9, "/test-cards/card-28.svg", "{}"},
    {"test_card_29", "Test Leviathan", "A 10-cost 10/11 minion with no special effect.", 10, 10, 11, "/test-cards/card-29.svg", "{}"},
    {"test_card_30", "Test Worldbreaker", "A 10-cost 11/10 minion with no special effect.", 10, 11, 10, "/test-cards/card-30.svg", "{}"},
}};

void apply_test_card_seed(Cards &card, const TestCardSeed &seed)
{
    card.setCardKey(seed.cardKey);
    card.setName(seed.name);
    card.setDescription(seed.description);
    card.setManaCost(seed.manaCost);
    card.setAttack(seed.attack);
    card.setHealth(seed.health);
    card.setCardType("minion");
    card.setRarity("common");
    card.setClassType("unused");
    card.setEffectJson(seed.effectJson);
    card.setImageUrl(seed.imageUrl);
    card.setIsCollectible(true);
}

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
            apply_test_card_seed(card, seed);
            cardMapper.insert(card);
            cardId = card.getValueOfId();
        }
        else
        {
            auto card = std::move(cards.front());
            apply_test_card_seed(card, seed);
            cardMapper.update(card);
            cardId = card.getValueOfId();
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
        [cb, username, password, Pmapper, client] (std::vector<Users> users)->void
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
