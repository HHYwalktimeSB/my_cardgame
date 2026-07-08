#include "UserController.h"
#include "models/Users.h"
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include"session_check.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

using ResponseCallback = std::function<void(const drogon::HttpResponsePtr &)>;

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
                [cb](Users)->void
                {
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
