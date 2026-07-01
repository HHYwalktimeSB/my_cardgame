#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class ProfileController : public drogon::HttpController<ProfileController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProfileController::getProfile, "/profile/stat", Get);
    METHOD_LIST_END

    void getProfile(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback
    );

    void query_cards(const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);
};