#include "DeckController.h"
#include<jsoncpp/json/json.h>
#include"session_check.h"
#include<models/Decks.h>
#include<models/DeckCards.h>
#include<models/PlayerCards.h>
#include<unordered_map>
#include"MatchController.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::cardgame_db;



void DeckController::get_all_decks(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_with_error("not logged in", k401Unauthorized, callback);
        return;
    }
    auto dbClient = app().getDbClient("db");
    try
    {
        Mapper<Decks> deckMapper(dbClient);
        auto user_decks = deckMapper.findBy(Criteria(Decks::Cols::_user_id, CompareOperator::EQ, userId));
        Json::Value json;
        json["state"] = "SUCCESS";
        json["decks"] = Json::Value(Json::arrayValue);
        for (auto& deck : user_decks){
            Json::Value tmp;
            tmp["deck_id"] = deck.getValueOfId();
            tmp["name"] = deck.getValueOfName();
            tmp["class_type"] = deck.getValueOfClassType();
            tmp["is_active"] = deck.getValueOfIsActive(); 
            json["decks"].append(std::move(tmp));
        }
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    }catch(const DrogonDbException &e){
        respond_with_error(e.base().what(),k500InternalServerError, callback);
    }
}

inline static std::vector<drogon_model::cardgame_db::DeckCards> get_deck_card_impl(drogon::orm::DbClientPtr dbClient, int64_t deckid){
    Mapper<DeckCards> deckCardMapper(dbClient);
    return deckCardMapper.findBy(Criteria(DeckCards::Cols::_deck_id, CompareOperator::EQ, deckid));
} 

void DeckController::get_deck(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &deckid_str)
{
    int64_t userId;
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_with_error("not logged in", k401Unauthorized, callback);
        return;
    }
    int64_t deck_id = this->get_deckid_from_string(deckid_str, callback);
    if(deck_id<0)return;
    auto dbClient = app().getDbClient("db");
    try{
        Mapper<Decks> deckMapper(dbClient);
        auto deck = deckMapper.findBy(Criteria(Decks::Cols::_id, CompareOperator::EQ, deck_id));
        if(deck.empty()||deck[0].getValueOfUserId() != userId){
            this->respond_with_error("invaild deckid", k400BadRequest, callback);
            return;
        }
        auto cards = get_deck_card_impl(dbClient, deck_id);
        Json::Value json;
        json["state"] = "SUCCESS";
        json["name"] = deck[0].getValueOfName();
        json["class_type"] = deck[0].getValueOfClassType();
        json["is_active"] = deck[0].getValueOfIsActive();
        json["cards"] = Json::Value(Json::arrayValue);
        int32_t counter = 0;
        for(auto & elem: cards){
            Json::Value tmp;
            tmp["card_id"] = elem.getValueOfCardId();
            tmp["quantity"] = elem.getValueOfQuantity();
            counter += elem.getValueOfQuantity();
            json["cards"].append(std::move(tmp));
        }
        json["count_cards"] = counter;
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    }catch(const DrogonDbException &e){
        respond_with_error(e.base().what(),k500InternalServerError, callback);
    }
}

void DeckController::put_deck(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    int64_t userId;
    std::function<void(const HttpResponsePtr &)> cb = std::move(callback);
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_with_error("not logged in", k401Unauthorized, cb);
        return;
    }
    bool incomplete;
    std::vector<std::pair<int64_t, int32_t> > cards;
    std::string name;
    if(!parse_request_json(req,name,cards, incomplete, cb))return;
    incomplete = incomplete || !check_player_owns_cards(userId, cards);
    int64_t deckid = -1;
    auto dbClient = app().getDbClient("db");
    auto trans = dbClient->newTransaction();
    Mapper<Decks> deckMapper(trans);
    Mapper<DeckCards> deckCardMapper(trans);
    try{
        Decks deck;
        deck.setUserId(userId);
        deck.setName(name);
        deck.setClassType("unused");
        deck.setIsActive(!incomplete);
        deckMapper.insert(deck);
        deckid = deck.getValueOfId();

        for(auto elem : cards) {
            DeckCards deckCard;
            deckCard.setDeckId(deckid);
            deckCard.setCardId(elem.first);
            deckCard.setQuantity(elem.second);
            deckCardMapper.insert(deckCard);
        }
        trans->setCommitCallback([cb, incomplete, deckid](bool success)->void {
            Json::Value respjson;
            if(success){
                respjson["state"] = "SUCCESS";
                respjson["incomplete"] = incomplete;
                respjson["deck_id"] = deckid;
            }else{
                respjson["state"] = "FAIL";
                respjson["message"] = "cannot write to database";
            }
            auto resp = HttpResponse::newHttpJsonResponse(respjson);
            if(!success)resp->setStatusCode(k500InternalServerError);
            cb(resp);
        });
    }catch(const DrogonDbException &e){
        trans->rollback();
        respond_with_error(e.base().what(),k500InternalServerError, cb);
    }
}

void DeckController::modify_deck(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback, const std::string &deckid_str)
{
    int64_t userId;
    std::function<void(const HttpResponsePtr &)> cb = std::move(callback);
    if(MySessionChecker::check_session(req, userId) != MySessionChecker::SessionOk){
        this->respond_with_error("not logged in", k401Unauthorized, cb);
        return;
    }
    if(MatchmakingService::is_player_in_match(userId)){
        this->respond_with_error("player in match or matchmaking", k409Conflict, cb);
        return;
    }
    int64_t deck_id = this->get_deckid_from_string(deckid_str, cb);
    if(deck_id<0)return;
    bool incomplete;
    std::vector<std::pair<int64_t, int32_t> > cards;
    std::string name;
    auto dbClient = app().getDbClient("db");
    {
        Mapper<Decks> deckMapper(dbClient);
        auto deck = deckMapper.findBy(Criteria(Decks::Cols::_id, CompareOperator::EQ, deck_id));
        if(deck.empty()||deck[0].getValueOfUserId() != userId){
            this->respond_with_error("invaild deckid", k400BadRequest, cb);
            return;
        }
        if(!parse_request_json(req,name,cards, incomplete, cb))return;
        incomplete = incomplete || !check_player_owns_cards(userId, cards);
    }
    auto trans = dbClient->newTransaction();
    Mapper<Decks> deckMapper(trans);
    Mapper<DeckCards> deckCardMapper(trans);
    try{
        auto deck = deckMapper.findByPrimaryKey(deck_id);
        if(deck.getValueOfUserId()!=userId){
            trans->rollback();
            this->respond_with_error("invaild deckid", k400BadRequest, cb);
            return;
        }
        deck.setName(name);
        deck.setIsActive(!incomplete);
        deckMapper.update(deck);
        deckCardMapper.deleteBy(Criteria(DeckCards::Cols::_deck_id, CompareOperator::EQ, deck_id));
        
        for(auto elem : cards) {
            DeckCards deckCard;
            deckCard.setDeckId(deck_id);
            deckCard.setCardId(elem.first);
            deckCard.setQuantity(elem.second);
            deckCardMapper.insert(deckCard);
        }
        trans->setCommitCallback([cb, incomplete](bool success)->void {
            Json::Value respjson;
            if(success){
                respjson["state"] = "SUCCESS";
                respjson["incomplete"] = incomplete;
            }else{
                respjson["state"] = "FAIL";
                respjson["message"] = "cannot write to database";
            }
            auto resp = HttpResponse::newHttpJsonResponse(respjson);
            if(!success)resp->setStatusCode(k500InternalServerError);
            cb(resp);
        });
    }catch(const DrogonDbException &e){
        trans->rollback();
        respond_with_error(e.base().what(),k500InternalServerError, cb);
    }
}

std::vector<int64_t> DeckController::GetDeckById(int64_t user_id, int64_t deck_id, bool &result_vaild)
{
    auto dbClient = app().getDbClient("db");
    Mapper<Decks> deckMapper(dbClient);
    std::vector<int64_t>res;
    auto deck = deckMapper.findBy(Criteria(Decks::Cols::_id, CompareOperator::EQ, deck_id));
    if(deck.empty()||deck[0].getValueOfUserId() != user_id){
        result_vaild = false;
        return res;
    }
    if(deck.empty() || deck[0].getValueOfUserId() != user_id || !deck[0].getValueOfIsActive()) {
        result_vaild = false;
        return res;
    }
    auto cards = get_deck_card_impl(dbClient, deck_id);
    for(auto & elem: cards){
        auto cardid = elem.getValueOfCardId();
        for(auto counter = elem.getValueOfQuantity(); counter>0 ; -- counter)
            res.push_back(cardid);
    }
    result_vaild = true;
    return res;
}

bool DeckController::parse_request_json(const HttpRequestPtr &req, 
    std::string &deckname, std::vector<std::pair<int64_t, int32_t> > &cards, bool& incomplete,
    std::function<void(const HttpResponsePtr &)> &callback)
{
    auto json = req->getJsonObject();
    if(!json){
        respond_with_error("missing json body", k400BadRequest, callback);
        return false;
    }
    if(!json->isMember("name") || !(*json)["name"].isString()){
        respond_with_error("missing deck name", k400BadRequest, callback);
        return false;
    }
    if(!json->isMember("cards") || !(*json)["cards"].isArray()){
        respond_with_error("cards must be array", k400BadRequest, callback);
        return false;
    }
    incomplete = false;
    int count_cards = 0;
    cards.reserve(40);
    deckname = (*json)["name"].asString();
    
    std::unordered_map<int64_t, int32_t> card_map;
    const auto &cardsJson = (*json)["cards"];
    for(const auto &item : cardsJson){
        if(!item.isObject()){
            respond_with_error("card item must be object", k400BadRequest, callback);
            return false;
        }

        if(!item.isMember("card_id") || !item["card_id"].isInt64()){
            respond_with_error("missing card_id", k400BadRequest, callback);
            return false;
        }

        if(!item.isMember("quantity") || !item["quantity"].isInt()){
            respond_with_error("missing quantity", k400BadRequest, callback);
            return false;
        }
        int64_t cardId = item["card_id"].asInt64();
        int quantity = item["quantity"].asInt();
        if(cardId <= 0 || quantity <= 0 ){
            respond_with_error("invalid card_id or quantity", k400BadRequest, callback);
            return false;
        }
        if(!card_map.insert(std::make_pair(cardId, quantity)).second){
            card_map.find(cardId)->second += quantity;
        }
        count_cards += quantity;
    }
    for(auto pair : card_map){
        cards.push_back(pair);
        if(pair.second>2)incomplete = true;
    }
    incomplete = incomplete || count_cards != 30;

    return true;
    
}

void DeckController::respond_with_error(const std::string &message, HttpStatusCode code, std::function<void(const HttpResponsePtr &)> &callback)
{
    Json::Value res;
    res["state"] = "ERROR";
    res["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(res);
    resp->setStatusCode(code);
    callback(resp);
}

int64_t DeckController::get_deckid_from_string(const std::string &str, std::function<void(const HttpResponsePtr &)> &callback)
{
    int64_t id;
    try{
        id = std::stol(str);
    }
    catch(std::invalid_argument){
        id = -1;
    }
    if(id<0)
        respond_with_error("invaild deckid", k400BadRequest, callback);
    return id;
}

bool DeckController::check_player_owns_cards(int64_t userId,
    const std::vector<std::pair<int64_t, int32_t>> &cards)
{
    auto dbClient = app().getDbClient("db");
    Mapper<PlayerCards> playerCardMapper(dbClient);

    auto ownedCards = playerCardMapper.findBy(
        Criteria(PlayerCards::Cols::_user_id, CompareOperator::EQ, userId)
    );

    std::unordered_map<int64_t, int32_t> owned;
    for(const auto &card : ownedCards) {
        owned[card.getValueOfCardId()] = card.getValueOfQuantity();
    }

    for( auto& card : cards) {
        int64_t cardId = card.first;
        int32_t needQuantity = card.second;

        auto it = owned.find(cardId);
        if(it == owned.end() || it->second < needQuantity) {
            return false;
        }
    }

    return true;
}
