# Card Game

一个用于练习 Drogon 和 C++17 的简单 PvP 卡牌游戏。项目当前重点是战斗房间、卡牌效果结算，以及低开销的前后端状态同步。

## 技术栈

- 后端：C++17、Drogon、JsonCpp
- 前端：React 18、TypeScript、Vite
- 数据库：PostgreSQL
- 实时通信：WebSocket

后端保存权威战斗状态。前端只提交玩家操作，并根据后端产生的事件更新界面，不在本地独立计算战斗结果。

## 当前功能

- 用户注册、登录和 Session Cookie 鉴权
- 卡牌目录和玩家卡牌收藏
- 创建、查看和修改卡组
- 匹配队列和 PvP 房间
- 出牌、随从攻击、结束回合和投降
- 战吼、亡语、伤害、治疗、增益和死亡结算
- WebSocket 增量事件同步、断线重连和事件补发
- 房间快照恢复

## 本地开发

需要安装 C++17 编译器、CMake、Drogon、PostgreSQL、Node.js 和 npm。

首先创建 `cardgame_db` 数据库和 `cardgame_user` 用户。默认开发配置见 `config.json`，数据库结构见 `db/schema.sql`。

```bash
psql -U cardgame_user -d cardgame_db -f db/schema.sql
```

构建后端并安装前端依赖：

```bash
cmake -S . -B build
cmake --build build -j

cd frontend
npm install
cd ..
```

启动前后端开发服务：

```bash
./start-dev.sh
```

默认地址：

- 前端：`http://127.0.0.1:5173`
- 后端：`http://127.0.0.1:5555`
- PostgreSQL：`127.0.0.1:5432`

Vite 会把 `/user`、`/profile`、`/cards`、`/decks`、`/matchfind` 和 `/battleroom` 代理到后端，并为 `/battleroom` 启用 WebSocket 代理。生产部署方式见 [deploy/README.md](deploy/README.md)。

## 状态同步概览

普通业务使用 HTTP。匹配成功后，战斗同步过程如下：

1. 前端调用 `GET /battleroom/{roomId}/snapshot` 获取完整快照。
2. 前端使用快照中的 `last_sequence` 建立 `/battleroom/ws` WebSocket。
3. 玩家操作通过 WebSocket 发送，后端完成权威结算。
4. 后端返回操作确认，并向双方推送结算事件。
5. 前端按事件的 `sequence` 顺序增量更新本地快照。
6. WebSocket 断开时前端暂停操作，每 4 秒尝试重连，并从最后收到的 `sequence` 继续补事件。
7. 如果所需事件已经从服务端缓存中淘汰，服务端发送 `error_require_snapshot`，前端重新获取完整快照。

战斗过程不会降级为 HTTP 长轮询。HTTP `/battleroom/{roomId}/operation` 暂时保留给压测脚本和调试工具，浏览器客户端不使用该接口。

## WebSocket 连接

连接地址：

```text
ws://host/battleroom/ws?room_id={roomId}&sequence={lastSequence}
```

HTTPS 页面对应使用 `wss://`。WebSocket 握手携带现有 Session Cookie；用户未登录或不属于该房间时，后端会关闭连接。

`sequence` 表示客户端已经处理的最后一个事件序号。连接建立后，服务端会发送所有 `sequence` 更大的可见事件。不同玩家可能收到不同的私有事件，例如只有抽牌玩家能看到抽到的具体卡牌。

## 紧凑数组协议

协议使用 JSON 文本帧，但高频的客户端消息、操作确认和事件内容使用定长数组，避免重复传输字段名。

所有整数 ID 均按 JSON 整数发送。数组的字段位置属于协议的一部分，修改顺序时必须同时更新前后端。

### 消息类型

| 值 | 方向 | 含义 |
|---:|---|---|
| `0` | 客户端 → 服务端 | 同步事件序号 |
| `1` | 客户端 → 服务端 | 提交战斗操作 |
| `2` | 服务端 → 客户端 | 操作确认或错误 |

### 同步请求

格式：

```json
[0, sequence]
```

| 下标 | 名称 | 含义 |
|---:|---|---|
| `0` | `message_type` | 固定为 `0` |
| `1` | `sequence` | 客户端已经处理的最后一个事件序号 |

示例：

```json
[0, 128]
```

除连接 URL 中的 `sequence` 外，客户端在 WebSocket 打开后还会发送一次同步请求，以当前最新序号为准。

### 战斗操作

格式：

```json
[1, request_id, version, operation_type, card_instance, target, target_type]
```

| 下标 | 名称 | 含义 |
|---:|---|---|
| `0` | `message_type` | 固定为 `1` |
| `1` | `request_id` | 客户端生成的操作编号，用于幂等处理和匹配确认 |
| `2` | `version` | 客户端操作所基于的房间版本 |
| `3` | `operation_type` | 操作类型，见下表 |
| `4` | `card_instance` | 手牌或攻击者的实例 ID；不适用时为 `0` |
| `5` | `target` | 目标实例 ID 或玩家 ID；无目标时为 `-1` |
| `6` | `target_type` | `0` 为随从，`1` 为英雄 |

操作类型：

| 值 | 含义 |
|---:|---|
| `0` | 出牌 |
| `1` | 随从攻击 |
| `2` | 结束回合 |
| `3` | 投降 |

例如，客户端基于版本 12，用实例 301 攻击玩家 9：

```json
[1, 42, 12, 1, 301, 9, 1]
```

`request_id` 在同一玩家的操作流中应保持唯一。如果客户端不确定某个操作是否已被处理，可以使用同一个 `request_id` 重发；服务端会返回缓存结果，不会重复结算。

### 操作确认

格式：

```json
[2, request_id, error_code, version]
```

| 下标 | 名称 | 含义 |
|---:|---|---|
| `0` | `message_type` | 固定为 `2` |
| `1` | `request_id` | 对应操作请求中的编号 |
| `2` | `error_code` | `0` 表示成功，其他值见下表 |
| `3` | `version` | 服务端当前房间版本 |

错误码：

| 值 | 名称 | 含义 |
|---:|---|---|
| `0` | `None` | 操作成功 |
| `1` | `PlayerNotInRoom` | 玩家不在房间中 |
| `2` | `RoomFinished` | 对局已经结束 |
| `3` | `NotYourTurn` | 当前不是该玩家的回合 |
| `4` | `InvalidCard` | 卡牌或卡牌实例无效 |
| `5` | `InvalidTarget` | 目标无效 |
| `6` | `InsufficientMana` | 法力不足 |
| `7` | `BoardFull` | 随从区已满 |
| `8` | `StaleVersion` | 客户端房间版本过旧 |

成功示例：

```json
[2, 42, 0, 13]
```

操作确认只说明请求是否被接受。具体状态变化由紧随其后的事件批次表达，客户端不应根据操作请求自行推演结果。

### 服务端事件批次

服务端事件使用一个带说明字段的外层对象：

```json
{
  "state": "SUCCESS",
  "transport": "websocket",
  "batch": {
    "room_version": 13,
    "first_sequence": 129,
    "format": 1,
    "events": [
      [11, 8, -1, 301, 9, 1, 4],
      [0, -1, -1, -1, -1, 0, 0]
    ]
  }
}
```

当同一批事件具有相同 `room_version`，并且 `sequence` 连续时，`format` 为 `1`，每个事件采用紧凑数组：

```json
[event_type, actor_id, card_id, card_instance, target_id, target_type, value]
```

| 下标 | 名称 | 含义 |
|---:|---|---|
| `0` | `event_type` | 事件类型，见下表 |
| `1` | `actor_id` | 发起或所属玩家 ID；不适用时为 `-1` |
| `2` | `card_id` | 卡牌定义 ID；不可见或不适用时为 `-1` |
| `3` | `card_instance` | 卡牌实例 ID；不适用时为 `-1` |
| `4` | `target_id` | 目标实例或玩家 ID；不适用时为 `-1` |
| `5` | `target_type` | `0` 为随从，`1` 为英雄 |
| `6` | `value` | 伤害、治疗、增益、赢家槽位等事件数值 |

批次内第 `i` 个事件的完整同步信息为：

```text
room_version = batch.room_version
sequence     = batch.first_sequence + i
```

事件类型：

| 值 | 名称 | 含义 |
|---:|---|---|
| `0` | `game_end` | 对局结束；`value` 为赢家槽位，`-1` 表示平局 |
| `1` | `player_1_start_turn` | 玩家槽位 1 开始回合 |
| `2` | `player_2_start_turn` | 玩家槽位 2 开始回合 |
| `3` | `card_play` | 打出卡牌 |
| `4` | `player_1_drawcard` | 玩家槽位 1 抽牌 |
| `5` | `player_2_drawcard` | 玩家槽位 2 抽牌 |
| `6` | `card_discard` | 手牌被丢弃 |
| `7` | `card_destory` | 手牌被销毁；名称保留当前协议中的拼写 |
| `8` | `player_1_fatigue` | 玩家槽位 1 受到疲劳伤害 |
| `9` | `player_2_fatigue` | 玩家槽位 2 受到疲劳伤害 |
| `10` | `error_require_snapshot` | 事件缓存不足，客户端必须重新获取快照 |
| `11` | `minion_attack` | 随从攻击 |
| `12` | `minion_dead` | 随从死亡 |
| `13` | `effect_damage` | 卡牌效果造成伤害 |
| `14` | `effect_heal` | 卡牌效果产生治疗 |
| `15` | `effect_buff` | 卡牌效果提供增益 |

这里的 `player_1`、`player_2` 指房间内部槽位，不是数据库用户 ID。当前前端中 `player_1_*` 对应快照的 `player_0`，`player_2_*` 对应快照的 `player_1`。

如果事件不满足“相同版本且序号连续”的条件，服务端会回退为带字段名的事件对象。客户端应同时兼容两种格式。对象事件包含 `type`、`room_version`、`sequence`、`value`，并按需包含 `actor_id`、`card_id`、`card_instance`、`target_id` 和 `target_type`。

## 版本、序号与恢复规则

`version` 和 `sequence` 用途不同：

- `version` 表示房间状态经过了多少次成功操作，用于拒绝基于旧状态提交的操作。
- `sequence` 标识单个结算事件，用于去重、排序和断线补发。一次操作可能产生多个连续事件。
- 操作成功时 `version` 增加一次，该操作产生的所有事件使用新的房间版本。
- 客户端只在完整处理事件后推进本地 `sequence`。
- 收到 `StaleVersion` 时，客户端应等待或补齐服务端事件后再操作。
- 收到 `error_require_snapshot` 时，客户端调用快照接口，用返回的 `version` 和 `last_sequence` 覆盖本地同步游标。

完整快照使用普通 JSON 对象而不是紧凑数组，因为快照发送频率低，并且可读性更重要。快照只向当前查看者暴露自己的具体手牌，对手只提供 `hand_count`。

## 主要接口

| 方法 | 路径 | 用途 |
|---|---|---|
| `POST` | `/user/register` | 注册 |
| `POST` | `/user/login` | 登录 |
| `POST` | `/user/logout` | 退出登录 |
| `GET` | `/user/status` | 检查登录状态 |
| `GET` | `/profile/stat` | 玩家资料和战绩 |
| `GET` | `/cards/catalog` | 卡牌目录 |
| `GET` | `/cards/my` | 玩家收藏 |
| `GET/POST` | `/decks/` | 查询或创建卡组 |
| `GET/POST` | `/decks/{deckId}` | 查询或修改卡组 |
| `POST` | `/matchfind/join` | 加入匹配队列 |
| `GET` | `/matchfind/poll` | 查询匹配结果 |
| `POST` | `/matchfind/cancel` | 取消匹配 |
| `GET` | `/battleroom/current` | 查询当前房间 |
| `GET` | `/battleroom/{roomId}/snapshot` | 获取完整战斗快照 |
| `GET` | `/battleroom/{roomId}/stat` | 查询房间结果 |
| `POST` | `/battleroom/{roomId}/leave` | 离开房间 |
| `WS` | `/battleroom/ws` | 战斗操作和事件同步 |

## 测试与构建检查

```bash
cmake -S . -B build
cmake --build build -j
./build/test/card_game_test

cd frontend
npm run build
```

负载测试脚本和说明位于 `loadtest/`。当前负载测试仍通过保留的 HTTP operation 接口产生战斗操作；测量浏览器实际协议带宽时，应使用 WebSocket 客户端或后续将脚本迁移到上述紧凑数组协议。
