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
const password = 'stress-pass';

const operationDuration = new Trend('battle_operation_duration', true);
const operationFailed = new Rate('battle_operation_failed');
const snapshotFailed = new Rate('battle_snapshot_failed');
const websocketErrors = new Counter('battle_websocket_errors');
const websocketEvents = new Counter('battle_websocket_events');
const websocketConnections = new Counter('battle_websocket_connections');

export const options = {
  setupTimeout: '5m',
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
  const response = http.post(
    `${baseUrl}/battleroom/${roomId}/operation`,
    JSON.stringify({
      type: operation.type,
      version: operation.version,
      request_id: player.requestId++,
      card_instance: operation.cardInstance || 0,
      target: operation.target == null ? -1 : operation.target,
      target_type: operation.targetType || 'minion',
    }),
    {
      jar: player.jar,
      headers: { 'Content-Type': 'application/json' },
      tags: { name: 'battle_operation' },
    },
  );
  operationDuration.add(Date.now() - startedAt);
  const succeeded = response.status === 200 && response.json('state') === 'SUCCESS';
  operationFailed.add(!succeeded);
  return succeeded;
}

function connectEvents(roomId, player) {
  let intentionalClose = false;
  const socket = new WebSocket(
    `${websocketBaseUrl}/battleroom/ws?room_id=${roomId}&sequence=0`,
    null,
    { jar: player.jar, tags: { name: 'battle_events' } },
  );
  socket.addEventListener('open', () => websocketConnections.add(1));
  socket.addEventListener('message', event => {
    try {
      const message = JSON.parse(event.data);
      websocketEvents.add((message.events || []).length);
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
  };
}

export function setup() {
  const battles = [];
  for (let matchNumber = 1; matchNumber <= matches; matchNumber += 1) {
    const firstPlayer = makePlayer(`stress_${runId}_${matchNumber * 2 - 1}`);
    const secondPlayer = makePlayer(`stress_${runId}_${matchNumber * 2}`);
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

  const firstSocket = connectEvents(roomId, firstPlayer);
  const secondSocket = connectEvents(roomId, secondPlayer);
  let actionRunning = false;

  const actionTimer = setInterval(() => {
    if (actionRunning) return;
    actionRunning = true;
    try {
      const publicSnapshot = getSnapshot(roomId, firstPlayer.jar);
      if (!publicSnapshot || publicSnapshot.room_state !== 'playing') return;
      const actor = publicSnapshot.current_player === firstPlayer.userId
        ? firstPlayer
        : secondPlayer;
      const snapshot = actor === firstPlayer
        ? publicSnapshot
        : getSnapshot(roomId, secondPlayer.jar);
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
          target: target.instance_id,
        });
      } else if (actorState.board.length < 7 && snapshot.my_hand.length > 0) {
        sendOperation(roomId, actor, {
          type: 'play card',
          version: snapshot.version,
          cardInstance: snapshot.my_hand[0].instance_id,
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
