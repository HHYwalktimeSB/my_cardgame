import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  cancelMatch,
  getCurrentRoom,
  getProfile,
  getRoomStat,
  getSnapshot,
  getStatus,
  joinMatch,
  leaveRoom,
  login,
  logout,
  pollMatch,
  pollRoom,
  register,
  sendOperation,
} from './api';
import type { BattleContext, OperationResult, Profile, RoomEvent, RoomSnapshot, View } from './types';

const TEST_CARD_NAME = '测试用卡牌';

function cardName(cardId: number) {
  return cardId === 1 ? TEST_CARD_NAME : `占位卡牌 #${cardId}`;
}

function formatEvent(event: RoomEvent) {
  if (event.type === 'card_play') return `使用 ${cardName(event.actor_id)}`;
  if (event.type === 'game_end') return event.value < 0 ? '对战结束：平局' : `对战结束：玩家 ${event.value} 获胜`;
  if (event.type.includes('drawcard')) return '抽取一张卡牌';
  if (event.type.includes('start_turn')) return '回合开始';
  if (event.type.includes('fatigue')) return `疲劳伤害 ${event.value}`;
  if (event.type === 'error_require_snapshot') return '需要重新同步快照';
  return event.type;
}

function eventKey(event: RoomEvent) {
  return `${event.sequence}:${event.room_version}:${event.type}:${event.actor_id}:${event.value}`;
}

function mergeEvents(current: RoomEvent[], incoming: RoomEvent[]) {
  const seen = new Set(current.map(eventKey));
  const uniqueIncoming = incoming.filter(event => {
    const key = eventKey(event);
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
  return [...uniqueIncoming, ...current].slice(0, 18);
}

export default function App() {
  const [view, setView] = useState<View>('login');
  const [profile, setProfile] = useState<Profile | null>(null);
  const [battle, setBattle] = useState<BattleContext | null>(null);
  const [notice, setNotice] = useState('');
  const noticeTimer = useRef<number | null>(null);

  const showNotice = useCallback((message: string) => {
    setNotice(message);
    if (noticeTimer.current) window.clearTimeout(noticeTimer.current);
    noticeTimer.current = window.setTimeout(() => {
      setNotice('');
      noticeTimer.current = null;
    }, 5000);
  }, []);

  const loadProfile = useCallback(async () => {
    const data = await getProfile();
    setProfile(data);
    return data;
  }, []);

  const restoreCurrentRoom = useCallback(async () => {
    const current = await getCurrentRoom();
    if (
      !current.in_room ||
      current.room_id == null ||
      current.match_id == null ||
      current.opponent_id == null
    ) {
      setBattle(null);
      return false;
    }
    setBattle({
      roomId: current.room_id,
      matchId: current.match_id,
      opponentId: current.opponent_id,
    });
    setView(current.room_state === 'finished' ? 'result' : 'room');
    return true;
  }, []);

  useEffect(() => {
    getStatus()
      .then(() => loadProfile())
      .then(() => restoreCurrentRoom())
      .then(restored => {
        if (!restored) setView('profile');
      })
      .catch(() => setView('login'));
  }, [loadProfile, restoreCurrentRoom]);

  async function handleLogout() {
    await logout();
    setProfile(null);
    setBattle(null);
    setView('login');
  }

  return (
    <div className="app-shell">
      <header className="topbar">
        <div>
          <div className="brand">Card Battle</div>
          <div className="subtitle">Drogon PvP test client</div>
        </div>
        <nav>
          {profile && (
            <>
              <button onClick={() => setView('profile')}>Profile</button>
              <button onClick={() => setView('queue')}>Find Match</button>
              <button onClick={handleLogout}>Logout</button>
            </>
          )}
        </nav>
      </header>

      {notice && <div className="toast">{notice}</div>}

      {view === 'login' && (
        <LoginScreen
          onDone={async message => {
            showNotice(message);
            await loadProfile();
            const restored = await restoreCurrentRoom();
            if (!restored) setView('profile');
          }}
        />
      )}

      {view === 'profile' && profile && (
        <ProfileScreen profile={profile} onFindMatch={() => setView('queue')} />
      )}

      {view === 'queue' && (
        <QueueScreen
          onMatched={match => {
            if (
              match.room_id == null ||
              match.match_id == null ||
              match.opponent_id == null
            ) {
              showNotice('Matched response is missing room data');
              return;
            }
            setBattle({
              roomId: match.room_id,
              matchId: match.match_id,
              opponentId: match.opponent_id,
            });
            setView('room');
          }}
          onNotice={showNotice}
        />
      )}

      {view === 'room' && battle && (
        <BattleRoom
          battle={battle}
          selfId={profile?.user_id ?? -1}
          onFinished={() => setView('result')}
          onNotice={showNotice}
        />
      )}

      {view === 'result' && battle && (
        <ResultScreen battle={battle} selfId={profile?.user_id ?? -1} onDone={() => setView('profile')} />
      )}
    </div>
  );
}

function LoginScreen({ onDone }: { onDone: (message: string) => void }) {
  const [username, setUsername] = useState('test1');
  const [password, setPassword] = useState('123456');
  const [mode, setMode] = useState<'login' | 'register'>('login');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');

  async function submit() {
    setBusy(true);
    setError('');
    try {
      const result =
        mode === 'login' ? await login(username, password) : await register(username, password);
      if (mode === 'register') {
        await login(username, password);
      }
      onDone(typeof result === 'string' ? result : 'success');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'request failed');
    } finally {
      setBusy(false);
    }
  }

  return (
    <main className="login-layout">
      <section className="login-panel">
        <div className="panel-kicker">Test Client</div>
        <h1>Card Battle</h1>
        <label>
          Username
          <input value={username} onChange={event => setUsername(event.target.value)} />
        </label>
        <label>
          Password
          <input
            type="password"
            value={password}
            onChange={event => setPassword(event.target.value)}
          />
        </label>
        {error && <div className="error-line">{error}</div>}
        <button className="primary-action" disabled={busy} onClick={submit}>
          {busy ? 'Working...' : mode === 'login' ? 'Login' : 'Register'}
        </button>
        <button className="text-action" onClick={() => setMode(mode === 'login' ? 'register' : 'login')}>
          {mode === 'login' ? 'Create test account' : 'Use existing account'}
        </button>
      </section>
    </main>
  );
}

function ProfileScreen({ profile, onFindMatch }: { profile: Profile; onFindMatch: () => void }) {
  const stats = profile.stats ?? {};
  return (
    <main className="page-grid">
      <section className="profile-card">
        <div className="avatar-ring">{profile.username.slice(0, 2).toUpperCase()}</div>
        <div>
          <div className="panel-kicker">Player Profile</div>
          <h2>{profile.username}</h2>
          <p>User #{profile.user_id}</p>
        </div>
        <button className="primary-action" onClick={onFindMatch}>
          Find Match
        </button>
      </section>

      <section className="stat-grid">
        <Metric label="Wins" value={stats.wins ?? 0} />
        <Metric label="Losses" value={stats.losses ?? 0} />
        <Metric label="Draws" value={stats.draws ?? 0} />
        <Metric label="Matches" value={stats.total_matches ?? 0} />
      </section>

      <section className="wide-panel">
        <div className="section-title">Decks</div>
        <div className="deck-row active">
          <div>
            <strong>Placeholder Deck</strong>
            <span>后端当前会自动填充 30 张 id=1 的测试用卡牌</span>
          </div>
          <span>deck id: 1</span>
        </div>
      </section>
    </main>
  );
}

function Metric({ label, value }: { label: string; value: number }) {
  return (
    <div className="metric">
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

function QueueScreen({
  onMatched,
  onNotice,
}: {
  onMatched: (match: Awaited<ReturnType<typeof joinMatch>>) => void;
  onNotice: (message: string) => void;
}) {
  const [queueing, setQueueing] = useState(false);
  const [status, setStatus] = useState('Ready');
  const pollTimer = useRef<number | null>(null);

  useEffect(() => {
    return () => {
      if (pollTimer.current) window.clearTimeout(pollTimer.current);
    };
  }, []);

  const poll = useCallback(async () => {
    try {
      const result = await pollMatch();
      if (result.status === 'MATCHED') {
        setQueueing(false);
        setStatus('Matched');
        onMatched(result);
        return;
      }
      setStatus(result.status);
      pollTimer.current = window.setTimeout(poll, 600);
    } catch (err) {
      setStatus(err instanceof Error ? err.message : 'poll failed');
      setQueueing(false);
    }
  }, [onMatched]);

  async function start() {
    setQueueing(true);
    setStatus('Joining queue');
    try {
      const result = await joinMatch(1);
      if (result.status === 'MATCHED') {
        onMatched(result);
        return;
      }
      setStatus(result.message ?? 'Waiting for opponent');
      poll();
    } catch (err) {
      setQueueing(false);
      setStatus(err instanceof Error ? err.message : 'join failed');
    }
  }

  async function cancel() {
    if (pollTimer.current) window.clearTimeout(pollTimer.current);
    try {
      await cancelMatch();
      onNotice('Matchmaking cancelled');
    } catch (err) {
      onNotice(err instanceof Error ? err.message : 'cancel failed');
    }
    setQueueing(false);
    setStatus('Ready');
  }

  return (
    <main className="match-layout">
      <section className="wide-panel">
        <div className="section-title">Deck Selection</div>
        <div className="deck-row active">
          <div>
            <strong>Placeholder Deck</strong>
            <span>30 x {TEST_CARD_NAME}</span>
          </div>
          <span>Selected</span>
        </div>
        <div className="placeholder-cards">
          {Array.from({ length: 5 }, (_, index) => (
            <MiniCard key={index} label={TEST_CARD_NAME} />
          ))}
        </div>
      </section>
      <aside className="queue-panel">
        <div className="status-orb" />
        <h2>{status}</h2>
        <p>当前阶段只使用固定 deck id 1 进行匹配测试。</p>
        <button className="primary-action" disabled={queueing} onClick={start}>
          Find Match
        </button>
        <button disabled={!queueing} onClick={cancel}>
          Cancel
        </button>
      </aside>
    </main>
  );
}

function BattleRoom({
  battle,
  selfId,
  onFinished,
  onNotice,
}: {
  battle: BattleContext;
  selfId: number;
  onFinished: () => void;
  onNotice: (message: string) => void;
}) {
  const [snapshot, setSnapshot] = useState<RoomSnapshot | null>(null);
  const [events, setEvents] = useState<RoomEvent[]>([]);
  const [sequence, setSequence] = useState(0);
  const [version, setVersion] = useState(0);
  const [selectedCard, setSelectedCard] = useState(0);
  const [busy, setBusy] = useState(false);
  const requestCounter = useRef(1);
  const polling = useRef(false);
  const sequenceRef = useRef(0);
  const versionRef = useRef(0);

  const refreshSnapshot = useCallback(async () => {
    const data = await getSnapshot(battle.roomId);
    if (data.version != null) {
      versionRef.current = data.version;
      setVersion(data.version);
    }
    if (data.last_sequence != null) {
      sequenceRef.current = data.last_sequence;
      setSequence(data.last_sequence);
    }
    setSnapshot(data);
  }, [battle.roomId]);

  useEffect(() => {
    refreshSnapshot().catch(err => onNotice(err instanceof Error ? err.message : 'snapshot failed'));
  }, [refreshSnapshot, onNotice]);

  useEffect(() => {
    if (!snapshot) return;
    polling.current = true;
    let timer: number | null = null;

    async function loop() {
      if (!polling.current) return;
      try {
        const result = await pollRoom(battle.roomId, sequenceRef.current);
        const newEvents = result.events ?? [];
        if (newEvents.some(event => event.type === 'error_require_snapshot')) {
          await refreshSnapshot();
        } else if (newEvents.length > 0) {
          const nextSequence = Math.max(sequenceRef.current, ...newEvents.map(event => event.sequence));
          const nextVersion = Math.max(versionRef.current, ...newEvents.map(event => event.room_version));
          sequenceRef.current = nextSequence;
          versionRef.current = nextVersion;
          setEvents(current => mergeEvents(current, newEvents));
          setSequence(nextSequence);
          setVersion(nextVersion);
          if (newEvents.some(event => event.type === 'game_end')) onFinished();
        }
      } catch {
        timer = window.setTimeout(loop, 1200);
        return;
      }
      timer = window.setTimeout(loop, 500);
    }

    timer = window.setTimeout(loop, 300);
    return () => {
      polling.current = false;
      if (timer) window.clearTimeout(timer);
    };
  }, [battle.roomId, onFinished, refreshSnapshot, snapshot]);

  const players = useMemo(() => {
    if (!snapshot) return null;
    const first = snapshot.player_0;
    const second = snapshot.player_1;
    return first.user_id === selfId ? { you: first, opponent: second } : { you: second, opponent: first };
  }, [selfId, snapshot]);

  async function operate(type: 'play card' | 'end turn' | 'surrender') {
    if (!snapshot || busy) return;
    setBusy(true);
    try {
      const result: OperationResult = await sendOperation(
        battle.roomId,
        type,
        version,
        requestCounter.current++,
        selectedCard,
      );
      if (result.state !== 'SUCCESS') {
        onNotice(result.action_error ?? result.message ?? 'operation failed');
        return;
      }
      setVersion(result.version ?? version);
      versionRef.current = result.version ?? versionRef.current;
      const newEvents = result.events ?? [];
      if (newEvents.length) {
        const nextSequence = Math.max(sequenceRef.current, ...newEvents.map(event => event.sequence));
        const nextVersion = Math.max(
          result.version ?? versionRef.current,
          ...newEvents.map(event => event.room_version),
        );
        sequenceRef.current = nextSequence;
        versionRef.current = nextVersion;
        setEvents(current => mergeEvents(current, newEvents));
        setSequence(nextSequence);
        setVersion(nextVersion);
      }
      await refreshSnapshot();
      if (newEvents.some(event => event.type === 'game_end')) onFinished();
    } catch (err) {
      onNotice(err instanceof Error ? err.message : 'operation failed');
    } finally {
      setBusy(false);
    }
  }

  if (!snapshot || !players) {
    return <main className="loading-panel">Loading room {battle.roomId}...</main>;
  }

  return (
    <main className="battle-shell">
      <section className="room-header">
        <span>Room {battle.roomId}</span>
        <span>Match {battle.matchId}</span>
        <span>Connected</span>
      </section>

      <PlayerStrip label="Opponent" userId={players.opponent.user_id} health={players.opponent.health} />

      <section className="board">
        <div className="opponent-hand">
          {Array.from({ length: players.opponent.hand_count }, (_, index) => (
            <div className="card-back" key={index} />
          ))}
        </div>
        <BoardRow cards={players.opponent.board} />
        <BoardRow cards={players.you.board} active />
      </section>

      <section className="hand-row">
        {snapshot.my_hand.map((cardId, index) => (
          <button
            className={`hand-card ${selectedCard === index ? 'selected' : ''}`}
            key={`${cardId}-${index}`}
            onClick={() => setSelectedCard(index)}
          >
            <span>{cardName(cardId)}</span>
            <small>id {cardId}</small>
          </button>
        ))}
      </section>

      <PlayerStrip label="You" userId={players.you.user_id} health={players.you.health} />

      <aside className="battle-actions">
        <div className="mana-block">
          <span>Mana</span>
          <strong>
            {players.you.mana}/{players.you.max_mana}
          </strong>
        </div>
        <div className="mana-block">
          <span>Deck</span>
          <strong>{players.you.deck_count}</strong>
        </div>
        <button className="primary-action" disabled={busy || snapshot.my_hand.length === 0} onClick={() => operate('play card')}>
          Play Card
        </button>
        <button disabled={busy} onClick={() => operate('end turn')}>
          End Turn
        </button>
        <button disabled={busy} onClick={() => operate('surrender')}>
          Surrender
        </button>
        <div className="event-log">
          <div className="section-title">Events</div>
          {events.length === 0 && <p>No events yet</p>}
          {events.map(event => (
            <div className="event-line" key={`${event.sequence}-${event.type}`}>
              #{event.sequence} {formatEvent(event)}
            </div>
          ))}
        </div>
      </aside>
    </main>
  );
}

function PlayerStrip({ label, userId, health }: { label: string; userId: number; health: number }) {
  return (
    <section className="player-strip">
      <div className="avatar-ring small">{label.slice(0, 1)}</div>
      <div>
        <strong>{label}</strong>
        <span>Player #{userId}</span>
      </div>
      <div className="health-pill">{health}</div>
    </section>
  );
}

function BoardRow({ cards, active = false }: { cards: number[]; active?: boolean }) {
  return (
    <div className={`board-row ${active ? 'active' : ''}`}>
      {Array.from({ length: 7 }, (_, index) => (
        <div className="board-slot" key={index}>
          {cards[index] ? cardName(cards[index]) : ''}
        </div>
      ))}
    </div>
  );
}

function MiniCard({ label }: { label: string }) {
  return (
    <div className="mini-card">
      <span>{label}</span>
    </div>
  );
}

function ResultScreen({
  battle,
  selfId,
  onDone,
}: {
  battle: BattleContext;
  selfId: number;
  onDone: () => void;
}) {
  const [message, setMessage] = useState('Loading result...');

  useEffect(() => {
    getRoomStat(battle.roomId)
      .then(stat => {
        if (stat.room_state !== 'finished') {
          setMessage('Room is not finished yet');
          return;
        }
        if (!stat.winner || stat.winner < 0) setMessage('Draw');
        else setMessage(stat.winner === selfId ? 'You win' : `Player ${stat.winner} wins`);
      })
      .catch(err => setMessage(err instanceof Error ? err.message : 'stat failed'));
  }, [battle.roomId, selfId]);

  async function leave() {
    try {
      await leaveRoom(battle.roomId);
    } finally {
      onDone();
    }
  }

  return (
    <main className="result-panel">
      <div className="panel-kicker">Result</div>
      <h1>{message}</h1>
      <button className="primary-action" onClick={leave}>
        Leave Room
      </button>
    </main>
  );
}
