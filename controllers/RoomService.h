#ifndef ROOM_SERVICE_H_
#define ROOM_SERVICE_H_

#include<queue>
#include<inttypes.h>
#include<mutex>
#include<deque>
#include<chrono>
#include<unordered_map>
#include<unordered_set>
#include<jsoncpp/json/json.h>
#include<drogon/WebSocketConnection.h>
#include<bits/shared_ptr.h>
#include"BattleState.h"

struct MatchInfo{
    int64_t players[2];
    int64_t decks[2];
    int64_t matchId;
};

class BattleRoom
{
public:
    using PlayerState = cardgame::PlayerState;
    using RoomStatus = cardgame::RoomStatus;
    using OperationType = cardgame::OperationType;
    using TargetType = cardgame::TargetType;
    using Operation = cardgame::Operation;
    using EventVisibility = cardgame::EventVisibility;
    using RoomEventType = cardgame::RoomEventType;
    using RoomEvent = cardgame::RoomEvent;
    using EventVector = cardgame::EventVector;
    using ActionError = cardgame::ActionError;
    using ActionResult = cardgame::ActionResult;
    ActionResult applyOperation(int64_t userId, const Operation &operation);
    EventVector getEventsAfter(uint64_t sequence, uint64_t viewerId, bool& is_viewer_valid);
    Json::Value getEventsAfterJson(uint64_t sequence, uint64_t viewerId, bool& is_viewer_valid);
    bool containPlayer(int64_t playerid)const{
        std::lock_guard<std::mutex> guard(mutex_);
        return state_.players[0].userId==playerid || state_.players[1].userId == playerid;
    }
    bool isFinished()const{
        std::lock_guard<std::mutex> guard(mutex_);
        auto ret = state_.status == RoomStatus::Finished;
        return ret;
    }
    RoomStatus getStatus_and_winner(int64_t& win){
        std::lock_guard<std::mutex> guard(mutex_);
        if(state_.winner!=-1)win = state_.players[state_.winner%2].userId;
        else win = -1;
        return state_.status;
    }
    int64_t getWinner()const{
        int64_t win = -1;
        std::lock_guard<std::mutex> guard(mutex_);
        if(state_.winner!=-1)win = state_.players[state_.winner%2].userId;
        return win;
    }
    bool isFinishedFor(std::chrono::steady_clock::duration duration)const;
    bool markPlayerLeft(int64_t playerid, bool& both_left);
    bool init_with_match_info(const MatchInfo & match);
    void setRoomId(int64_t roomid) { std::lock_guard<std::mutex> guard(mutex_); roomId_ = roomid; }
    int64_t getRoomId()const { std::lock_guard<std::mutex> guard(mutex_); return roomId_; }
    int64_t getPlayer1Id()const { std::lock_guard<std::mutex> guard(mutex_); return state_.players[0].userId; }
    int64_t getPlayer2Id()const { std::lock_guard<std::mutex> guard(mutex_); return state_.players[1].userId; }
    static constexpr int max_requestid_stored = 64;
    static constexpr int max_event_stored = 256;
    struct RoomSnapshot{
        struct Card
        {
            int64_t instanceId;
            int64_t cardId;
            int attack;
            int health;
            int maxHealth;
            bool exhausted;
            bool valid;
        };
        int64_t roomId, matchId;
        uint64_t version, last_sequence;
        RoomStatus status;
        int64_t winnerId;
        int64_t currentPlayer;
        struct PublicPlayerState{
            int64_t userId;
            int health;
            int mana;
            int maxMana;
            int deck_count;
            int hand_count;
            std::vector<Card> board;
        };
        PublicPlayerState players[2];
        std::vector<Card> my_hand;
    };
    struct QuickStateGettingStruct{
        bool function_success;
        RoomStatus state;
        int64_t roomId;
        int64_t matchId;
        int64_t opponentId;
    };
    QuickStateGettingStruct quickStateGetting(int64_t playerid);
    bool getSnapshotBin(int64_t viewerId, RoomSnapshot& snapshot);
    static Json::Value eventListToJson(const EventVector &ev);
private:
    void get_events_impl(EventVector& event, uint64_t seq, int64_t viewer);
    void get_snapshot_impl(RoomSnapshot& ret, int64_t viewer);
    inline RoomEvent create_event(
        RoomEventType type,
        EventVisibility visibility,
        int64_t cardId,
        int64_t actorid,
        int64_t instanceid,
        int64_t targetid,
        int value){
        return {nextSequence++, version_, type, visibility, cardId, actorid, targetid, instanceid, value};
    }
    mutable std::mutex mutex_;

    int64_t roomId_;
    int64_t matchId_;

    cardgame::BattleState state_;
    bool playerLeft_[2]{false, false};
    bool hasFinishedAt_{false};
    std::chrono::steady_clock::time_point finishedAt_;

    uint64_t version_{0};
    uint64_t nextSequence{1};

    std::deque<RoomEvent> events;
    std::queue<uint64_t> processed_op_q[2];
    std::unordered_map<uint64_t, ActionResult> processed_op[2];

};

#include<functional>

class RoomService {
  public:
    using SerializedEventArray = std::shared_ptr<const std::string>;

    std::shared_ptr<BattleRoom> createRoom(const MatchInfo &match);

    std::shared_ptr<BattleRoom> findRoom(int64_t roomId);

    std::shared_ptr<BattleRoom> findRoomByPlayer(int64_t userId);
    bool is_player_in_room(int64_t userid);

    bool removeRoom(int64_t roomId);
    bool removeFinishedRoomIfExpired(int64_t roomId);
    bool playerLeaveRoom(int64_t roomId, int64_t userId);
    void scheduleFinishedRoomCleanup(int64_t roomId);
    static RoomService& GetServer();
    std::pair<bool, uint64_t> registerPoll(int roomId, int64_t userId, std::function<void()>&& callback);
    void invokePolls(int roomId);
    void invokePollWithToken(int64_t roomid, uint64_t tok);
    void registerWebSocket(
        int64_t roomId,
        int64_t userId,
        uint64_t sequence,
        const drogon::WebSocketConnectionPtr &connection);
    void unregisterWebSocket(const drogon::WebSocketConnectionPtr &connection);
    void syncWebSocket(
        const drogon::WebSocketConnectionPtr &connection,
        uint64_t sequence);
    static SerializedEventArray serializeEventArray(
        const BattleRoom::EventVector &events);
    static std::string serializeSnapshot(
        const BattleRoom::RoomSnapshot &snapshot);
    void publishWebSocketEvents(
        int64_t roomId,
        const BattleRoom::EventVector &sharedEvents,
        const SerializedEventArray &sharedEventArray);

    struct PollStruct{
        bool is_registered[2];
        uint64_t toks[2];
        int64_t players[2];
        std::function<void()> callback[2];
    };

  private:
    struct WebSocketSubscriber
    {
        int64_t userId;
        uint64_t sequence;
        std::weak_ptr<drogon::WebSocketConnection> connection;
        std::mutex mutex;
    };

    void sendWebSocketEvents(
        const std::shared_ptr<BattleRoom> &room,
        const std::shared_ptr<WebSocketSubscriber> &subscriber,
        const BattleRoom::EventVector *sharedEvents = nullptr,
        const SerializedEventArray &sharedEventArray = {});

    std::mutex mutex_;
    int64_t room_id_counter{0};
    std::unordered_map<int64_t, std::shared_ptr<BattleRoom>> rooms;
    std::unordered_map<int64_t, PollStruct >polls;
    std::unordered_map<
        int64_t,
        std::vector<std::shared_ptr<WebSocketSubscriber>>> websocketSubscribers;
    uint64_t poll_tok_gen;

    std::unordered_map<int64_t, int64_t>playerRooms;
};


#endif
