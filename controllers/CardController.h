#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class CardController : public drogon::HttpController<CardController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CardController::getMyCards, "/cards/my", Get);
    METHOD_LIST_END

    void getMyCards(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback
    );
};