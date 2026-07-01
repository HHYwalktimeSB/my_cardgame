#include "ProfileController.h"

#include <drogon/orm/Mapper.h>
#include <drogon/orm/Criteria.h>

#include "models/Users.h"
#include "models/PlayerStats.h"

using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

void ProfileController::getProfile(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback
)
{
    auto dbClient = drogon::app().getDbClient("db");

    auto userid = req->session()->getOptional<int64_t>("user_id");
    if(!userid){
        Json::Value error;
        error["error"] = "not logged in";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    try
    {
        Mapper<Users> userMapper(dbClient);
        Mapper<PlayerStats> statsMapper(dbClient);

        auto user = userMapper.findByPrimaryKey(*userid);

        auto statsList = statsMapper.findBy(
            Criteria(PlayerStats::Cols::_user_id, CompareOperator::EQ, *userid)
        );

        Json::Value result;
        result["user_id"] = static_cast<Json::Int64>(user.getValueOfId());
        result["username"] = user.getValueOfUsername();

        if (!statsList.empty())
        {
            const auto &stats = statsList[0];

            //result["stats"]["rating"] = stats.getValueOfRating();
            result["stats"]["wins"] = stats.getValueOfWins();
            result["stats"]["losses"] = stats.getValueOfLosses();
            result["stats"]["draws"] = stats.getValueOfDraws();
            result["stats"]["total_matches"] = stats.getValueOfTotalMatches();
        }
        else
        {
            result["stats"]["wins"] = 0;
            result["stats"]["losses"] = 0;
            result["stats"]["draws"] = 0;
            result["stats"]["total_matches"] = 0;
        }

        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k200OK);
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