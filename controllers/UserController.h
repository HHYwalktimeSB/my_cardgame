#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class UserController : public HttpController<UserController>
{
public:
    METHOD_LIST_BEGIN

    ADD_METHOD_TO(UserController::registerUser, "/user/register", Post);
    ADD_METHOD_TO(UserController::loginUser, "/user/login", Post);
    ADD_METHOD_TO(UserController::status, "/user/status", Get);
    ADD_METHOD_TO(UserController::logout, "/user/logout", Post);
    

    METHOD_LIST_END

    void registerUser(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

    void loginUser(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

    void status(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback);

    void logout(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback);
};
