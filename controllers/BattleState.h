#ifndef BATTLE_STATE_H_
#define BATTLE_STATE_H_

#include <cstdint>
#include <array>
#include <unordered_map>
#include <vector>

namespace cardgame
{

struct PlayerState
{
    int64_t userId;
    int64_t deckId;
    bool is_connected{false};

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

enum class RoomStatus
{
    Preparing,
    Playing,
    Finished
};

enum class CardType : uint8_t
{
    Minion,
    Spell
};

enum class TargetType{
    Minion,
    Hero
};

enum class CardZone : uint8_t
{
    Deck,
    Hand,
    Board,
    Graveyard,
    Discard
};

enum class EffectTrigger : uint8_t
{
    Battlecry,
    Deathrattle,
    Count
};

enum class EffectType : uint8_t
{
    Damage,
    Heal,
    Buff
};

enum class EffectTarget : uint8_t
{
    Selected,
    Self,
    FriendlyHero,
    EnemyHero
};

struct EffectDefinition
{
    EffectType type;
    EffectTarget target;
    int value;
};

using CardEffects = std::array<
    std::vector<EffectDefinition>,
    static_cast<size_t>(EffectTrigger::Count)>;

struct CardDefinition
{
    int32_t attack{0};
    int32_t health{0};
    int32_t manaCost{0};
    CardType type{CardType::Minion};
    CardEffects effects;
};

enum class OperationType
{
    PlayCard,
    Attack,
    EndTurn,
    Surrender
};

struct Operation
{
    OperationType type;
    TargetType targetType{TargetType::Minion};
    uint64_t requestId;
    uint64_t expectedVersion;
    int64_t cardInstanceId{0};
    int64_t targetId{0};
};

enum class EventVisibility
{
    Public,
    PlayerOneOnly,
    PlayerTwoOnly
};

enum class RoomEventType
{
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
    EventErr_snapshot_required,
    MinionAttack,
    MinionDead,
    EffectDamage,
    EffectHeal,
    EffectBuff,
};

struct RoomEvent
{
    uint64_t sequence;
    uint64_t roomVersion;
    RoomEventType type;
    EventVisibility visibility;
    int64_t cardId;
    int64_t actorId;
    int64_t targetId;
    int64_t instanceId;
    int value;
    TargetType targetType{TargetType::Minion};
};

using EventVector = std::vector<RoomEvent>;

enum class ActionError
{
    None,
    PlayerNotInRoom,
    RoomFinished,
    NotYourTurn,
    InvalidCard,
    InvalidTarget,
    InsufficientMana,
    BoardFull,
    StaleVersion
};

struct ActionResult
{
    ActionError error;
    uint64_t version;
    EventVector generatedEvents;
};

struct CardInstance
{
    int64_t instanceId;
    int64_t cardId;

    int8_t ownerIndex;
    CardZone zone;

    int attack;
    int health;
    int maxHealth;
    bool exhausted;
};

struct BattleState
{
    RoomStatus status{RoomStatus::Preparing};
    PlayerState players[2];
    int currentPlayer{0};
    int winner{-1};
    int64_t instanceIdCounter{0};
    std::unordered_map<int64_t, CardInstance> instances;
    std::unordered_map<int64_t, CardDefinition> cardDefinitions;
};

}

#endif
