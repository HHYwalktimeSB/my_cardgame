#include "CardController.h"

#include <drogon/drogon.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Criteria.h>

#include "models/Cards.h"
#include "models/PlayerCards.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

void CardController::getMyCards(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback
)
{
    auto session = req->session();
    int64_t userId = session->get<int64_t>("user_id");

    auto dbClient = app().getDbClient("db");

    try
    {
        Mapper<PlayerCards> playerCardMapper(dbClient);
        Mapper<Cards> cardMapper(dbClient);

        auto playerCards = playerCardMapper.findBy(Criteria(PlayerCards::Cols::_user_id, CompareOperator::EQ, userId) );

        Json::Value root;
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
        error["error"] = "database_error";
        error["message"] = e.base().what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}