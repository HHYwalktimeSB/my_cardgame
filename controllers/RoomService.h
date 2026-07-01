#ifndef ROOM_SERVICE_H_
#define ROOM_SERVICE_H_

#include<queue>
#include<inttypes.h>
#include<mutex>
#include<deque>
#include<unordered_map>
#include<unordered_set>
#include<jsoncpp/json/json.h>
#include<bits/shared_ptr.h>

struct MatchInfo{
    int64_t players[2];
    int64_t decks[2];
    int64_t matchId;
};

class BattleRoom
{
public:
struct PlayerState {
    int64_t userId;
    int64_t deckId;
    bool is_connected{0};

    int health{30};
    int mana{0};
    int maxMana{0};
    int maxMana_max{10};
    int hand_max{10};
    int fatigue_damage{1};

    std::vector<int64_t> deck;
    std::vector<int64_t> hand;
    std::vector<int64_t> board;
    };
    enum class RoomStatus {
    Preparing,
    Playing,
    Finished
    };

  enum class OperationType {
    PlayCard,
    Attack,
    EndTurn,
    Surrender
    };
    struct Operation{
        OperationType type;
        uint64_t requestId;
        uint64_t expectedVersion;
        int64_t cardInstanceId{0};
        int64_t targetId{0};
    };
    
    enum class EventVisibility {
      Public,
      PlayerOneOnly,
      PlayerTwoOnly
    };
    enum class RoomEventType{
    GameEnd,
    Player1_Turn,
    Player2_Turn,
    PlayCard,
    Player1_DrawCard,
    Player2_DrawCard,
    DiscardCard,
    DestoryCard,
    Player1_Fatigue,
    Player2_Fatigue,
    EventErr_snapshot_required
    };

    struct RoomEvent {
    uint64_t sequence;
    uint64_t roomVersion;
    RoomEventType type;
    EventVisibility visibility;
    int64_t actorId;
    int value;
    //Json::Value payload;
    };
  using EventVector = std::vector<RoomEvent>;
    enum class ActionError {
    None,
    PlayerNotInRoom,
    RoomFinished,
    NotYourTurn,
    InvalidCard,
    InvalidTarget,
    InsufficientMana,
    StaleVersion,
    //DuplicateRequest
  };

  struct ActionResult {
      ActionError error;
      uint64_t version;
      EventVector generatedEvents;
  };
    //int postOperation(int64_t playerid, const Operation&);//when a player post operation
    ActionResult applyOperation(int64_t userId, const Operation &operation);
    EventVector getEventsAfter(uint64_t sequence, uint64_t viewerId, bool& is_viewer_valid);
    Json::Value getEventsAfterJson(uint64_t sequence, uint64_t viewerId, bool& is_viewer_valid);
    bool containPlayer(int64_t playerid)const{
        std::lock_guard<std::mutex> guard(mutex_);
        return players[0].userId==playerid || players[1].userId == playerid;
    }
    bool isFinished()const{
        std::lock_guard<std::mutex> guard(mutex_);
        auto ret = status_ == RoomStatus::Finished;
        return ret;
    }
    RoomStatus getStatus_and_winnder(int64_t& win){
        std::lock_guard<std::mutex> guard(mutex_);
        if(winner!=-1)win = players[winner%2].userId;
        else win = -1;
        return status_;
    }
    int64_t getWinner()const{
        int64_t win = -1;
        std::lock_guard<std::mutex> guard(mutex_);
        if(winner!=-1)win = players[winner%2].userId;
        return win;
    }
    bool init_with_match_info(const MatchInfo & match);
    void setRoomId(int64_t roomid) { std::lock_guard<std::mutex> guard(mutex_); roomId_ = roomid; }
    int64_t getRoomId()const { std::lock_guard<std::mutex> guard(mutex_); return roomId_; }
    int64_t getPlayer1Id()const { std::lock_guard<std::mutex> guard(mutex_); return players[0].userId; }
    int64_t getPlayer2Id()const { std::lock_guard<std::mutex> guard(mutex_); return players[1].userId; }
    static constexpr int max_requestid_stored = 64;
    static constexpr int max_event_stored = 256;
    //实现snapshot接口
    struct RoomSnapshot{
        int64_t roomId, matchId;
        uint64_t version, last_sequence;
        RoomStatus status;
        int64_t winnerId;
        int currentPlayer;
        struct PublicPlayerState{
            int64_t userId;
            int health;
            int mana;
            int maxMana;
            int deck_count;
            int hand_count;
            std::vector<int64_t> board;
        };
        PublicPlayerState players[2];
        std::vector<int64_t>my_hand;
    };
    bool getSnapshotBin(int64_t viewerId, RoomSnapshot& snapshot);
    Json::Value getSnapshotJson(int viewerId, bool & is_vaild);
    static Json::Value eventListToJson(const EventVector &ev);
private:
    void get_events_impl(EventVector& event, uint64_t seq, int64_t viewer);
    bool game_update_playcard_(EventVector& _ev, const Operation& op);
    void get_snapshot_impl(RoomSnapshot& ret, int64_t viewer);
        //return false for invalid target
    void game_update_endturn_(EventVector& _ev);
    void game_update_startturn_(EventVector& _ev);
    inline RoomEvent create_event(RoomEventType type, EventVisibility visibility, int64_t actorid, int value){
        return {nextSequence++, version_, type, visibility, actorid, value};
    }
    bool player_drawcard_(EventVector& _ev, int which);

    struct TestField{
        bool player_1_play_card{0};
        bool player_2_play_card{0};
    };
    TestField testinfo;
    mutable std::mutex mutex_;

    int64_t roomId_;
    int64_t matchId_;

    RoomStatus status_{RoomStatus::Preparing};
    PlayerState players[2];

    int currentPlayer{0};
    int winner{-1};

    uint64_t version_{0};
    uint64_t nextSequence{1};

    std::deque<RoomEvent> events;
    std::queue<uint64_t> processed_op_q[2];
    std::unordered_map<uint64_t, ActionResult> processed_op[2];

};

class RoomService {
  public:
      std::shared_ptr<BattleRoom> createRoom(const MatchInfo &match);

      std::shared_ptr<BattleRoom> findRoom(int64_t roomId);

      std::shared_ptr<BattleRoom> findRoomByPlayer(int64_t userId);
      bool is_player_in_room(int64_t userid);

      bool removeRoom(int64_t roomId);
      static RoomService& GetServer();

  private:
      std::mutex mutex_;
      int64_t room_id_counter{0};
      std::unordered_map<int64_t, std::shared_ptr<BattleRoom>> rooms;

      std::unordered_map<int64_t, int64_t>playerRooms;
  };


#endif
