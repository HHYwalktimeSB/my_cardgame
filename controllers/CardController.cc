#include "CardController.h"

#include <drogon/drogon.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Criteria.h>

#include "models/Cards.h"
#include "models/PlayerCards.h"
#include "session_check.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

void CardController::getCardCatalog(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback
)
{
    (void)req;
    auto dbClient = app().getDbClient("db");

    try
    {
        Mapper<Cards> cardMapper(dbClient);
        auto cards = cardMapper.findAll();

        Json::Value root;
        root["state"] = "SUCCESS";
        root["cards"] = Json::arrayValue;

        for(const auto &card : cards)
        {
            Json::Value item;
            item["id"] = static_cast<Json::Int64>(card.getValueOfId());
            item["card_key"] = card.getValueOfCardKey();
            item["name"] = card.getValueOfName();
            item["mana_cost"] = card.getValueOfManaCost();
            item["card_type"] = card.getValueOfCardType();
            item["is_collectible"] = card.getValueOfIsCollectible();

            if(card.getDescription())
                item["description"] = card.getValueOfDescription();
            else
                item["description"] = Json::Value();

            if(card.getAttack())
                item["attack"] = card.getValueOfAttack();
            else
                item["attack"] = Json::Value();

            if(card.getHealth())
                item["health"] = card.getValueOfHealth();
            else
                item["health"] = Json::Value();

            if(card.getRarity())
                item["rarity"] = card.getValueOfRarity();
            else
                item["rarity"] = Json::Value();

            if(card.getClassType())
                item["class_type"] = card.getValueOfClassType();
            else
                item["class_type"] = Json::Value();

            if(card.getEffectJson())
                item["effect_json"] = card.getValueOfEffectJson();
            else
                item["effect_json"] = Json::Value();

            if(card.getImageUrl())
                item["image_url"] = card.getValueOfImageUrl();
            else
                item["image_url"] = Json::Value();

            root["cards"].append(std::move(item));
        }

        root["count"] = static_cast<int>(root["cards"].size());

        auto resp = HttpResponse::newHttpJsonResponse(root);
        callback(resp);
    }
    catch (const DrogonDbException &e)
    {
        Json::Value error;
        error["state"] = "ERROR";
        error["error"] = "database_error";
        error["message"] = e.base().what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void CardController::getMyCards(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback
)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk)
    {
        Json::Value error;
        error["state"] = "ERROR";
        error["error"] = "not logged in";
        error["message"] = "user need to logged in to call the api";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto dbClient = app().getDbClient("db");

    try
    {
        Mapper<PlayerCards> playerCardMapper(dbClient);
        Mapper<Cards> cardMapper(dbClient);

        auto playerCards = playerCardMapper.findBy(Criteria(PlayerCards::Cols::_user_id, CompareOperator::EQ, userId) );

        Json::Value root;
        root["state"] = "SUCCESS";
        root["cards"] = Json::arrayValue;

        for (const auto &playerCard : playerCards)
        {
            int64_t cardId = playerCard.getValueOfCardId();
            int quantity = playerCard.getValueOfQuantity();
            //auto card = cardMapper.findByPrimaryKey(cardId);
            Json::Value item;
            item["id"] = static_cast<Json::Int64>(cardId);
            item["quantity"] = quantity;

            root["cards"].append(item);
        }

        root["count"] = static_cast<int>(root["cards"].size());

        auto resp = HttpResponse::newHttpJsonResponse(root);
        callback(resp);
    }
    catch (const DrogonDbException &e)
    {
        Json::Value error;
        error["state"] = "ERROR";
        error["error"] = "database_error";
        error["message"] = e.base().what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}
