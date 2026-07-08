#pragma once

#include <drogon/HttpController.h>
#include<vector>
#include<utility>


using namespace drogon;

class DeckController : public drogon::HttpController<DeckController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DeckController::get_all_decks, "/decks/", Get);
    ADD_METHOD_TO(DeckController::get_deck, "/decks/{1:deckid}", Get);
    ADD_METHOD_TO(DeckController::put_deck, "/decks/", Post);
    ADD_METHOD_TO(DeckController::modify_deck, "/decks/{1:deckid}", Post);
    METHOD_LIST_END

    void get_all_decks(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback
    );
    void get_deck(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& deckid_str
    );
    void put_deck(const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback
    );
    void modify_deck(const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string& deckid_str
    );

    //Note this function may throw a database exception;
    static std::vector<int64_t> GetDeckById(int64_t user_id, int64_t deck_id, bool& reault_vaild);
    static bool check_player_owns_cards(int64_t userId, const std::vector<std::pair<int64_t, int32_t>> &cards);
    private:
    bool parse_request_json(const HttpRequestPtr &req,
    std::string& deckname, std::vector<std::pair<int64_t, int32_t> >& cards, bool&,
    std::function<void(const HttpResponsePtr &)> &callback);
    void respond_with_error(const std::string &message, HttpStatusCode code,
         std::function<void(const HttpResponsePtr &)> &callback);
    int64_t get_deckid_from_string(const std::string& str, std::function<void(const HttpResponsePtr &)> &callback);
};