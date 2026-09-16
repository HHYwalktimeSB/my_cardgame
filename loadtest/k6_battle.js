import http from 'k6/http';
import { check } from 'k6';
import { Counter, Rate, Trend } from 'k6/metrics';
import { WebSocket } from 'k6/websockets';

const baseUrl = __ENV.BASE_URL || 'http://127.0.0.1:5555';
const websocketBaseUrl = baseUrl.replace(/^http/, 'ws');
const matches = Number(__ENV.MATCHES || 1);
const runId = __ENV.RUN_ID || 'local';
const durationSeconds = Number(__ENV.DURATION || 60);
const actionIntervalMilliseconds = Number(__ENV.ACTION_INTERVAL_MS || 750);
const slowOperationMilliseconds = Number(__ENV.SLOW_OPERATION_MS || 1000);
const slowOperationLogLimit = Number(__ENV.SLOW_OPERATION_LOG_LIMIT || 5);
const password = __ENV.PASSWORD || 'stress-pass';
const playerOffset = Number(__ENV.PLAYER_OFFSET || 0);

const operationDuration = new Trend('battle_operation_duration', true);
const operationMetrics = {
  attack: {
    count: new Counter('battle_operation_attack_count'),
    duration: new Trend('battle_operation_attack_duration', true),
  },
  'play card': {
    count: new Counter('battle_operation_play_card_count'),
    duration: new Trend('battle_operation_play_card_duration', true),
  },
  'end turn': {
    count: new Counter('battle_operation_end_turn_count'),
    duration: new Trend('battle_operation_end_turn_duration', true),
  },
};
const operationFailed = new Rate('battle_operation_failed');
const snapshotFailed = new Rate('battle_snapshot_failed');
const websocketErrors = new Counter('battle_websocket_errors');
const websocketEvents = new Counter('battle_websocket_events');
const websocketConnections = new Counter('battle_websocket_connections');
const websocketBytesReceived = new Counter('battle_websocket_bytes_received');
const websocketMessageSize = new Trend('battle_websocket_message_size', true);
let slowOperationLogs = 0;

const compactEventTypes = [
  'game_end', 'player_1_start_turn', 'player_2_start_turn', 'card_play',
  'player_1_drawcard', 'player_2_drawcard', 'card_discard', 'card_destory',
  'player_1_fatigue', 'player_2_fatigue', 'error_require_snapshot',
  'minion_attack', 'minion_dead', 'effect_damage', 'effect_heal', 'effect_buff',
];

function expandCompactEvent(event) {
  const [type, actorId, cardId, cardInstance, targetId, targetType, value] = event;
  return {
    type: compactEventTypes[type] || 'unknown',
    value,
    ...(actorId >= 0 ? { actor_id: actorId } : {}),
    ...(cardId >= 0 ? { card_id: cardId } : {}),
    ...(cardInstance >= 0 ? { card_instance: cardInstance } : {}),
    ...(targetId >= 0 ? { target_id: targetId } : {}),
    ...(targetType === 1 ? { target_type: 'hero' } : {}),
  };
}

export const options = {
  setupTimeout: __ENV.SETUP_TIMEOUT || '30m',
  scenarios: {
    battles: {
      executor: 'per-vu-iterations',
      vus: matches,
      iterations: 1,
      maxDuration: `${durationSeconds + 60}s`,
    },
  },
  thresholds: {
    battle_operation_failed: ['rate<0.01'],
    battle_operation_duration: ['p(95)<200'],
    battle_snapshot_failed: ['rate<0.01'],
    battle_websocket_errors: ['count<1'],
    battle_websocket_connections: [`count>=${matches * 2}`],
  },
};

function form(values) {
  return Object.entries(values)
    .map(([key, value]) => `${encodeURIComponent(key)}=${encodeURIComponent(value)}`)
    .join('&');
}

function login(username, jar) {
  const response = http.post(
    `${baseUrl}/user/login`,
    form({ username, password }),
    {
      jar,
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      tags: { name: 'login' },
    },
  );
  check(response, { 'login succeeds': result => result.status === 200 });
  return response.status === 200;
}

function getCardDefinitions(jar) {
  const response = http.get(`${baseUrl}/cards/catalog`, {
    jar,
    tags: { name: 'card_catalog' },
  });
  if (response.status !== 200) return {};
  return Object.fromEntries(response.json('cards').map(card => [Number(card.id), card]));
}

function applyEvents(snapshot, events, cards, selfId) {
  const players = [snapshot.player_0, snapshot.player_1];
  const playerById = id => players.find(player => player.user_id === id);
  const boardCard = id => players.flatMap(player => player.board)
    .find(card => card.instance_id === id);
  for (const event of events) {
    snapshot.version = Math.max(snapshot.version || 0, event.room_version || 0);
    snapshot.last_sequence = Math.max(snapshot.last_sequence || 0, event.sequence || 0);
    const actor = playerById(event.actor_id);
    if (event.type === 'card_play' && actor) {
      const card = cards[event.card_id];
      actor.mana = Math.max(0, actor.mana - Number(card?.mana_cost || 0));
      actor.hand_count = Math.max(0, actor.hand_count - 1);
      if (actor.user_id === selfId)
        snapshot.my_hand = snapshot.my_hand.filter(item => item.instance_id !== event.card_instance);
      if (card?.card_type === 'minion') actor.board.push({
        instance_id: event.card_instance, card_id: event.card_id,
        attack: Number(card.attack || 0), health: Number(card.health || 0),
        max_health: Number(card.health || 0), exhausted: true,
      });
    } else if (event.type === 'minion_attack') {
      const attacker = boardCard(event.card_instance);
      if (attacker) attacker.exhausted = true;
      if (event.target_type === 'hero') {
        const target = playerById(event.target_id);
        if (target) target.health -= event.value;
      } else {
        const target = boardCard(event.target_id);
        if (attacker && target) {
          attacker.health -= target.attack;
          target.health -= event.value;
        }
      }
    } else if (event.type === 'minion_dead') {
      for (const player of players)
        player.board = player.board.filter(card => card.instance_id !== event.card_instance);
    } else if (event.type === 'effect_damage' || event.type === 'effect_heal') {
      const direction = event.type === 'effect_damage' ? -1 : 1;
      const target = event.target_type === 'hero'
        ? playerById(event.target_id) : boardCard(event.target_id);
      if (target) target.health += direction * event.value;
    } else if (event.type === 'effect_buff') {
      const target = boardCard(event.target_id);
      if (target) {
        target.attack += event.value;
        target.health += event.value;
        target.max_health += event.value;
      }
    } else if (event.type.endsWith('_start_turn')) {
      const player = event.type === 'player_1_start_turn' ? players[0] : players[1];
      snapshot.current_player = player.user_id;
      player.max_mana = Math.min(10, player.max_mana + 1);
      player.mana = player.max_mana;
      player.board.forEach(card => { card.exhausted = false; });
      if (player.deck_count > 0) { player.deck_count -= 1; player.hand_count += 1; }
    } else if (event.type.endsWith('_drawcard') && actor?.user_id === selfId) {
      snapshot.my_hand.push({ instance_id: event.card_instance, card_id: event.card_id });
    } else if (event.type.endsWith('_fatigue') && actor) {
      actor.health -= event.value;
    } else if ((event.type === 'card_destory' || event.type === 'card_discard') && actor) {
      actor.hand_count = Math.max(0, actor.hand_count - 1);
      if (actor.user_id === selfId)
        snapshot.my_hand = snapshot.my_hand.filter(card => card.instance_id !== event.card_instance);
    } else if (event.type === 'game_end') snapshot.room_state = 'finished';
  }
}

function findDeck(jar) {
  const response = http.get(`${baseUrl}/decks/`, {
    jar,
    tags: { name: 'list_decks' },
  });
  if (response.status !== 200) return null;
  const expectedName = `stress-load-${runId}`;
  const deck = response.json('decks').find(item => item.name === expectedName);
  return deck ? deck.deck_id : null;
}

function getProfile(jar) {
  const response = http.get(`${baseUrl}/profile/stat`, {
    jar,
    tags: { name: 'profile' },
  });
  return response.status === 200 ? response.json('user_id') : null;
}

function getSnapshot(roomId, jar) {
  const response = http.get(`${baseUrl}/battleroom/${roomId}/snapshot`, {
    jar,
    tags: { name: 'battle_snapshot' },
  });
  snapshotFailed.add(response.status !== 200);
  return response.status === 200 ? response.json() : null;
}

function sendOperation(roomId, player, operation) {
  const startedAt = Date.now();
  const requestId = player.requestId++;
  const response = http.post(
    `${baseUrl}/battleroom/${roomId}/operation`,
    JSON.stringify({
      type: operation.type,
      version: operation.version,
      request_id: requestId,
      card_instance: operation.cardInstance || 0,
      target: operation.target == null ? -1 : operation.target,
      target_type: operation.targetType || 'minion',
    }),
    {
      jar: player.jar,
      headers: { 'Content-Type': 'application/json' },
      tags: { name: 'battle_operation', operation: operation.type },
    },
  );
  const duration = Date.now() - startedAt;
  operationDuration.add(duration);
  operationMetrics[operation.type].count.add(1);
  operationMetrics[operation.type].duration.add(duration);
  const succeeded = response.status === 200 && response.json('state') === 'SUCCESS';
  operationFailed.add(!succeeded);
  if (duration >= slowOperationMilliseconds && slowOperationLogs < slowOperationLogLimit) {
    slowOperationLogs += 1;
    console.warn(`slow battle operation ${JSON.stringify({
      room_id: roomId,
      player_id: player.userId,
      type: operation.type,
      card_id: operation.cardId || null,
      card_instance: operation.cardInstance || null,
      target_card_id: operation.targetCardId || null,
      target: operation.target == null ? null : operation.target,
      version: operation.version,
      request_id: requestId,
      duration_ms: duration,
      waiting_ms: response.timings.waiting,
      receiving_ms: response.timings.receiving,
      status: response.status,
      state: response.status === 200 ? response.json('state') : null,
    })}`);
  }
  return succeeded;
}

function connectEvents(roomId, player) {
  let intentionalClose = false;
  const sequence = player.snapshot?.last_sequence || 0;
  const socket = new WebSocket(
    `${websocketBaseUrl}/battleroom/ws?room_id=${roomId}&sequence=${sequence}`,
    null,
    { jar: player.jar, tags: { name: 'battle_events' } },
  );
  socket.addEventListener('open', () => websocketConnections.add(1));
  socket.addEventListener('message', event => {
    try {
      const messageBytes = typeof event.data === 'string'
        ? event.data.length
        : event.data.byteLength;
      websocketBytesReceived.add(messageBytes);
      websocketMessageSize.add(messageBytes);
      const message = JSON.parse(event.data);
      const batch = message.batch;
      const events = (batch?.events || message.events || []).map((rawItem, index) => {
        const item = Array.isArray(rawItem) ? expandCompactEvent(rawItem) : rawItem;
        return {
          ...item,
          room_version: item.room_version ?? batch?.room_version,
          sequence: item.sequence ?? ((batch?.first_sequence || 0) + index),
        };
      });
      websocketEvents.add(events.length);
      if (message.snapshot) player.snapshot = message.snapshot;
      else if (player.snapshot) applyEvents(player.snapshot, events, player.cards, player.userId);
    } catch {
      websocketErrors.add(1);
    }
  });
  socket.addEventListener('error', () => {
    if (!intentionalClose) websocketErrors.add(1);
  });
  return {
    close() {
      intentionalClose = true;
      socket.close();
    },
  };
}

function makePlayer(username) {
  return {
    username,
    jar: new http.CookieJar(),
    requestId: 1,
    userId: null,
    snapshot: null,
    cards: {},
  };
}

export function setup() {
  const battles = [];
  for (let matchNumber = 1; matchNumber <= matches; matchNumber += 1) {
    const firstPlayerNumber = playerOffset + matchNumber * 2 - 1;
    const secondPlayerNumber = playerOffset + matchNumber * 2;
    const firstPlayer = makePlayer(`stress_${runId}_${firstPlayerNumber}`);
    const secondPlayer = makePlayer(`stress_${runId}_${secondPlayerNumber}`);
    if (!login(firstPlayer.username, firstPlayer.jar) ||
        !login(secondPlayer.username, secondPlayer.jar)) {
      throw new Error(`login failed while preparing match ${matchNumber}`);
    }
    firstPlayer.userId = getProfile(firstPlayer.jar);
    secondPlayer.userId = getProfile(secondPlayer.jar);
    const firstDeckId = findDeck(firstPlayer.jar);
    const secondDeckId = findDeck(secondPlayer.jar);
    if (!firstDeckId || !secondDeckId || !firstPlayer.userId || !secondPlayer.userId) {
      throw new Error(`missing stress-test data for match ${matchNumber}`);
    }

    const firstJoin = http.post(
      `${baseUrl}/matchfind/join?deckid=${firstDeckId}`,
      null,
      { jar: firstPlayer.jar, tags: { name: 'match_join_setup' } },
    );
    const secondJoin = http.post(
      `${baseUrl}/matchfind/join?deckid=${secondDeckId}`,
      null,
      { jar: secondPlayer.jar, tags: { name: 'match_join_setup' } },
    );
    const roomId = secondJoin.status === 200 ? secondJoin.json('room_id') : null;
    if (firstJoin.status !== 200 || roomId == null) {
      throw new Error(`matchmaking failed for match ${matchNumber}`);
    }
    battles.push({
      roomId,
      first: { username: firstPlayer.username, userId: firstPlayer.userId },
      second: { username: secondPlayer.username, userId: secondPlayer.userId },
    });
  }
  return { battles };
}

export default function (data) {
  const preparedBattle = data.battles[__VU - 1];
  const firstPlayer = {
    username: preparedBattle.first.username,
    jar: new http.CookieJar(),
    requestId: 1,
    userId: preparedBattle.first.userId,
  };
  const secondPlayer = {
    username: preparedBattle.second.username,
    jar: new http.CookieJar(),
    requestId: 1,
    userId: preparedBattle.second.userId,
  };

  if (!login(firstPlayer.username, firstPlayer.jar) ||
      !login(secondPlayer.username, secondPlayer.jar)) {
    operationFailed.add(true);
    return;
  }
  const roomId = preparedBattle.roomId;
  const cardDefinitions = getCardDefinitions(firstPlayer.jar);
  const manaCosts = Object.fromEntries(
    Object.entries(cardDefinitions).map(([id, card]) => [id, Number(card.mana_cost)]),
  );
  firstPlayer.cards = cardDefinitions;
  secondPlayer.cards = cardDefinitions;
  firstPlayer.snapshot = getSnapshot(roomId, firstPlayer.jar);
  secondPlayer.snapshot = getSnapshot(roomId, secondPlayer.jar);
  if (!firstPlayer.snapshot || !secondPlayer.snapshot) {
    operationFailed.add(true);
    return;
  }

  const firstSocket = connectEvents(roomId, firstPlayer);
  const secondSocket = connectEvents(roomId, secondPlayer);
  let actionRunning = false;

  const actionTimer = setInterval(() => {
    if (actionRunning) return;
    actionRunning = true;
    try {
      const publicSnapshot = firstPlayer.snapshot;
      if (!publicSnapshot || publicSnapshot.room_state !== 'playing') return;
      const actor = publicSnapshot.current_player === firstPlayer.userId
        ? firstPlayer
        : secondPlayer;
      const snapshot = actor === firstPlayer ? publicSnapshot : secondPlayer.snapshot;
      if (!snapshot) return;

      const actorState = snapshot.player_0.user_id === actor.userId
        ? snapshot.player_0
        : snapshot.player_1;
      const opponentState = snapshot.player_0.user_id === actor.userId
        ? snapshot.player_1
        : snapshot.player_0;
      const attacker = actorState.board.find(card => !card.exhausted && card.attack > 0);
      const target = opponentState.board[0];

      if (attacker && target) {
        sendOperation(roomId, actor, {
          type: 'attack',
          version: snapshot.version,
          cardInstance: attacker.instance_id,
          cardId: attacker.card_id,
          target: target.instance_id,
          targetCardId: target.card_id,
        });
      } else if (actorState.board.length < 7) {
        const playableCard = snapshot.my_hand.find(card =>
          card.card_id && manaCosts[card.card_id] <= actorState.mana);
        if (!playableCard) {
          sendOperation(roomId, actor, {
            type: 'end turn',
            version: snapshot.version,
          });
          return;
        }
        sendOperation(roomId, actor, {
          type: 'play card',
          version: snapshot.version,
          cardInstance: playableCard.instance_id,
          cardId: playableCard.card_id,
        });
      } else {
        sendOperation(roomId, actor, {
          type: 'end turn',
          version: snapshot.version,
        });
      }
    } finally {
      actionRunning = false;
    }
  }, actionIntervalMilliseconds);

  setTimeout(() => {
    clearInterval(actionTimer);
    firstSocket.close();
    secondSocket.close();
  }, durationSeconds * 1000);
}
