#pragma once

#include <drogon/HttpController.h>
#include<jsoncpp/json/json.h>
//#include "myutilitys.h"
#include<unordered_map>
#include<unordered_set>
#include<mutex>
#include<queue>

struct WaitingPlayer
{
    int64_t userId;
    int64_t deckId;
    size_t generation;
};

struct Match
{
    int64_t matchId;
    int64_t opponentId;
    int64_t roomid;
};

using namespace drogon;

class MatchmakingService{
    protected:
    size_t callback_id_counter;
    std::queue<WaitingPlayer> waitingQueue;
    std::unordered_map<int64_t, size_t>waitingPlayers;
    std::mutex lock;
    std::unordered_map<int64_t, Match> result;
    int64_t match_id_counter;//for test only
    size_t player_generation_counter;
    public:
    using ResponseCallback = std::function<void(const drogon::HttpResponsePtr &)>;
    struct callback_struct{
        size_t id;
        std::function<void(const HttpResponsePtr &)> cb;
        callback_struct(size_t _id, ResponseCallback&& rval):id(_id),
            cb(std::move(rval)){}
        callback_struct(size_t _id,const ResponseCallback& lval):id(_id),
            cb(lval){}
        ~callback_struct()=default;
        callback_struct(callback_struct&& r):id(r.id),cb(std::move(r.cb)){}
        callback_struct(const callback_struct& l):id(l.id),cb(l.cb){}
        void operator=(const callback_struct& l){id = l.id; cb = l.cb;}
        void operator=(callback_struct&& r){id = r.id; cb = std::move(r.cb);}
    };
    protected:
    std::unordered_map<int64_t, callback_struct >callbacks;
    public:
    MatchmakingService();
    enum {Waiting, Matched,E_in_queue, E_in_match, E_cannot_create_room};
    Match joinMatch(int64_t userId, int64_t deckId, int& state);
    std::optional<ResponseCallback> cancelMatch(int64_t userId, bool&);
    std::optional<ResponseCallback> queryMatch(int64_t userId, size_t token, bool& is_matched, Match&matchres);
    std::optional<ResponseCallback> takeCallback(int64_t userid);
    bool remove_match(int64_t userid);
    enum class PollResultState{
        Added, Duplicate, Matched, NotJoined
    };
    struct PollRegisterResult{
        PollResultState state;
        size_t token;
        Match match;
    };
    PollRegisterResult RegisterPoll(int64_t userid, const ResponseCallback& cb);
};

class MatchController : public drogon::HttpController<MatchController>
{
    public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MatchController::poll, "/matchfind/poll", Get);
    ADD_METHOD_TO(MatchController::join, "/matchfind/join", Post);
    ADD_METHOD_TO(MatchController::cancel, "/matchfind/cancel", Post);
    METHOD_LIST_END
    void poll(const HttpRequestPtr &req, 
    std::function<void(const HttpResponsePtr &)> &&callback);
    void join(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback);
    void cancel(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback);
    bool remove_match(int64_t userid);
};