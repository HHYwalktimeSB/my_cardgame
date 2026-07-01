#include "UserController.h"
#include "models/Users.h"
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;

using ResponseCallback = std::function<void(const drogon::HttpResponsePtr &)>;

void sendTextResponse(const ResponseCallback& callback,
    drogon::HttpStatusCode code,
    const std::string &message)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setBody(message);
    callback(resp);
}

auto makeDbExceptionHandler(ResponseCallback callback){
    return [callback](const drogon::orm::DrogonDbException &e)->void
    {
        sendTextResponse(
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
        sendTextResponse(cb, k400BadRequest, "Need username and password!");
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
                sendTextResponse(cb, k400BadRequest, "User exists!");
                return;
            }

            Users user;

            user.setUsername(username);
            user.setPasswordHash(password);//TODO check passwd

            Pmapper->insert(user,
                [cb](Users)->void
                {
                    sendTextResponse(cb, k200OK, "Success!");
                },
                makeDbExceptionHandler(cb));
        },
        makeDbExceptionHandler(cb));
}

void UserController::loginUser(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto username = req->getParameter("username");
    auto password = req->getParameter("password");
    auto cb = std::move(callback);

    if(username.empty() || password.empty())
    {
        sendTextResponse(cb, k400BadRequest, "Need username and password!");
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
                sendTextResponse(cb,k401Unauthorized,"Invalid username");
                return;
            }

            const auto &user = users[0];

            if(user.getValueOfPasswordHash() != password)
            {
                sendTextResponse(cb, k401Unauthorized, "Incorrect password");
                return;
            }

            req->session()->insert("user_id", user.getValueOfId());

            sendTextResponse(cb, k200OK, "login success");
        },
        makeDbExceptionHandler(cb));
}

void UserController::status(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    ResponseCallback cb = std::move(callback);

    auto userid = req->session()->getOptional<int64_t>("user_id");
    auto client = app().getDbClient("db");
    auto mapper = std::make_shared<Mapper<Users>>(client);

    if(!userid)
    {
        sendTextResponse(cb, k401Unauthorized, "Not logged in");
        return;
    }

    mapper->findBy(
        Criteria(Users::Cols::_id, CompareOperator::EQ, *userid),
        [cb](std::vector<Users> users)->void
        {
            if(users.empty()){
                sendTextResponse(cb, k400BadRequest, "Session Error! User not exist!");
                return;
            }
            sendTextResponse(cb, k200OK, "Logged in as " + users[0].getValueOfUsername());
        },
        makeDbExceptionHandler(cb));
}

void UserController::logout(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto userid = req->session()->getOptional<int64_t>("user_id");
    ResponseCallback cb = std::move(callback);

    if(!userid){
        sendTextResponse(cb, k401Unauthorized, "Not logged in");
    }
    else{
        req->session()->erase("user_id");
        sendTextResponse(cb, k200OK, "Logout Success");
    }
}