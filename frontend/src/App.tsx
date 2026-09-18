import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  cancelMatch,
  createDeck,
  getCardCatalog,
  getCurrentRoom,
  getDeck,
  getDecks,
  getMyCards,
  getProfile,
  getRoomStat,
  getSnapshot,
  getStatus,
  joinMatch,
  leaveRoom,
  login,
  logout,
  pollMatch,
  register,
  roomWebSocketUrl,
  updateDeck,
} from './api';
import type {
  BattleContext,
  CardCatalogItem,
  DeckEditorState,
  DeckEntry,
  DeckSummary,
  OperationResult,
  OwnedCard,
  Profile,
  RoomEvent,
  RoomSnapshot,
  SnapshotCardRef,
  View,
} from './types';

function cardName(cardsById: Map<number, CardCatalogItem>, cardId?: number) {
  if (!cardId) return 'Unknown Card';
  return cardsById.get(cardId)?.name ?? `Card #${cardId}`;
}

type SelectedAction = {
  type: 'play card' | 'attack';
  instanceId: number;
  requiresTarget: boolean;
};

function hasSelectedBattlecry(card?: CardCatalogItem) {
  if (!card?.effect_json) return false;
  try {
    const effects = JSON.parse(card.effect_json) as {
      battlecry?: { target?: string }[];
    };
    return effects.battlecry?.some(effect => effect.target === 'selected') ?? false;
  } catch {
    return false;
  }
}

function formatEvent(cardsById: Map<number, CardCatalogItem>, event: RoomEvent) {
  if (event.type === 'card_play') {
    return `玩家 ${event.actor_id} 打出 ${cardName(cardsById, event.card_id)}`;
  }
  if (event.type === 'game_end') {
    return event.value < 0 ? '对战结束：平局' : `对战结束：玩家 ${event.value} 获胜`;
  }
  if (event.type.includes('drawcard')) return `${cardName(cardsById, event.card_id)} 被抽到手牌`;
  if (event.type.includes('start_turn')) return '新的回合开始';
  if (event.type.includes('fatigue')) return `疲劳伤害 ${event.value}`;
  if (event.type === 'error_require_snapshot') return '事件过旧，需要重新拉取快照';
  return event.type;
}

function eventKey(event: RoomEvent) {
  return [
    event.sequence,
    event.room_version,
    event.type,
    event.actor_id,
    event.card_id ?? 0,
    event.card_instance ?? 0,
    event.target_id ?? 0,
    event.value,
  ].join(':');
}

function mergeEvents(current: RoomEvent[], incoming: RoomEvent[]) {
  const seen = new Set(current.map(eventKey));
  const uniqueIncoming = incoming.filter(event => {
    const key = eventKey(event);
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
  return [...uniqueIncoming, ...current].slice(0, 24);
}

type RoomEventBatch = {
  room_version?: number;
  first_sequence?: number;
  format?: number;
  events?: Array<
    (Partial<RoomEvent> & Pick<RoomEvent, 'type' | 'value'>) |
    [number, number, number, number, number, number, number]
  >;
};

const compactEventTypes = [
  'game_end',
  'player_1_start_turn',
  'player_2_start_turn',
  'card_play',
  'player_1_drawcard',
  'player_2_drawcard',
  'card_discard',
  'card_destory',
  'player_1_fatigue',
  'player_2_fatigue',
  'error_require_snapshot',
  'minion_attack',
  'minion_dead',
  'effect_damage',
  'effect_heal',
  'effect_buff',
] as const;

function expandCompactEvent(event: number[], roomVersion: number, sequence: number): RoomEvent {
  const [type, actorId, cardId, cardInstance, targetId, targetType, value] = event;
  return {
    type: compactEventTypes[type] ?? 'unknown',
    room_version: roomVersion,
    sequence,
    value,
    ...(actorId >= 0 ? { actor_id: actorId } : {}),
    ...(cardId >= 0 ? { card_id: cardId } : {}),
    ...(cardInstance >= 0 ? { card_instance: cardInstance } : {}),
    ...(targetId >= 0 ? { target_id: targetId } : {}),
    ...(targetType === 1 ? { target_type: 'hero' as const } : {}),
  };
}

function expandEventBatch(batch?: RoomEventBatch) {
  if (!batch?.events) return [];
  return batch.events.map((event, index) => {
    const sequence = (batch.first_sequence ?? 0) + index;
    if (Array.isArray(event)) {
      return expandCompactEvent(event, batch.room_version ?? 0, sequence);
    }
    return {
      ...event,
      room_version: event.room_version ?? batch.room_version ?? 0,
      sequence: event.sequence ?? sequence,
    } as RoomEvent;
  });
}

function applyRoomEvents(
  current: RoomSnapshot,
  incoming: RoomEvent[],
  selfId: number,
  cardsById: Map<number, CardCatalogItem>,
) {
  const next: RoomSnapshot = {
    ...current,
    player_0: { ...current.player_0, board: current.player_0.board.map(card => ({ ...card })) },
    player_1: { ...current.player_1, board: current.player_1.board.map(card => ({ ...card })) },
    my_hand: current.my_hand.map(card => ({ ...card })),
  };
  const players = [next.player_0, next.player_1];
  const playerById = (id?: number) => players.find(player => player.user_id === id);
  const boardCard = (instanceId?: number) => {
    for (const player of players) {
      const card = player.board.find(item => item.instance_id === instanceId);
      if (card) return card;
    }
    return undefined;
  };

  for (const event of incoming) {
    next.version = Math.max(next.version ?? 0, event.room_version);
    next.last_sequence = Math.max(next.last_sequence ?? 0, event.sequence);
    const actor = playerById(event.actor_id);

    if (event.type === 'card_play' && actor && event.card_instance != null) {
      const definition = event.card_id == null ? undefined : cardsById.get(event.card_id);
      actor.mana = Math.max(0, actor.mana - (definition?.mana_cost ?? 0));
      actor.hand_count = Math.max(0, actor.hand_count - 1);
      if (actor.user_id === selfId) {
        next.my_hand = next.my_hand.filter(card => card.instance_id !== event.card_instance);
      }
      if (definition?.card_type === 'minion') {
        actor.board.push({
          instance_id: event.card_instance,
          card_id: event.card_id,
          attack: definition.attack ?? 0,
          health: definition.health ?? 0,
          max_health: definition.health ?? 0,
          exhausted: true,
        });
      }
    } else if (event.type === 'minion_attack') {
      const attacker = boardCard(event.card_instance);
      if (attacker) attacker.exhausted = true;
      if (event.target_type === 'hero') {
        const target = playerById(event.target_id);
        if (target) target.health -= event.value;
      } else {
        const target = boardCard(event.target_id);
        if (attacker && target) {
          attacker.health = (attacker.health ?? 0) - (target.attack ?? 0);
          target.health = (target.health ?? 0) - event.value;
        }
      }
    } else if (event.type === 'minion_dead' && event.card_instance != null) {
      for (const player of players) {
        player.board = player.board.filter(card => card.instance_id !== event.card_instance);
      }
    } else if (event.type === 'effect_damage' || event.type === 'effect_heal') {
      const direction = event.type === 'effect_damage' ? -1 : 1;
      if (event.target_type === 'hero') {
        const target = playerById(event.target_id);
        if (target) target.health += direction * event.value;
      } else {
        const target = boardCard(event.target_id);
        if (target) target.health = (target.health ?? 0) + direction * event.value;
      }
    } else if (event.type === 'effect_buff') {
      const target = boardCard(event.target_id);
      if (target) {
        target.attack = (target.attack ?? 0) + event.value;
        target.health = (target.health ?? 0) + event.value;
        target.max_health = (target.max_health ?? 0) + event.value;
      }
    } else if (event.type.endsWith('_start_turn')) {
      const player = event.type === 'player_1_start_turn' ? next.player_0 : next.player_1;
      next.current_player = player.user_id;
      player.max_mana = Math.min(10, player.max_mana + 1);
      player.mana = player.max_mana;
      player.board.forEach(card => { card.exhausted = false; });
      if (player.deck_count > 0) {
        player.deck_count -= 1;
        player.hand_count += 1;
      }
    } else if (event.type.endsWith('_drawcard') && actor?.user_id === selfId &&
               event.card_instance != null) {
      next.my_hand.push({ instance_id: event.card_instance, card_id: event.card_id });
    } else if (event.type.endsWith('_fatigue') && actor) {
      actor.health -= event.value;
    } else if ((event.type === 'card_destory' || event.type === 'card_discard') && actor) {
      actor.hand_count = Math.max(0, actor.hand_count - 1);
      if (event.card_instance != null && actor.user_id === selfId) {
        next.my_hand = next.my_hand.filter(card => card.instance_id !== event.card_instance);
      }
    } else if (event.type === 'game_end') {
      next.room_state = 'finished';
      next.winner_id = event.value < 0 ? -1 : players[event.value]?.user_id;
    }
  }
  return next;
}

function cardCount(entries: DeckEntry[]) {
  return entries.reduce((sum, entry) => sum + entry.quantity, 0);
}

function cardArtUrl(cardId?: number) {
  const safeId = cardId && cardId > 0 ? ((cardId - 1) % 30) + 1 : 1;
  return `/test-cards/card-${safeId}.svg`;
}

export default function App() {
  const [view, setView] = useState<View>('login');
  const [profile, setProfile] = useState<Profile | null>(null);
  const [battle, setBattle] = useState<BattleContext | null>(null);
  const [notice, setNotice] = useState('');
  const [catalog, setCatalog] = useState<CardCatalogItem[]>([]);
  const [ownedCards, setOwnedCards] = useState<OwnedCard[]>([]);
  const [decks, setDecks] = useState<DeckSummary[]>([]);
  const [deckEditor, setDeckEditor] = useState<DeckEditorState | null>(null);
  const noticeTimer = useRef<number | null>(null);

  const cardsById = useMemo(
    () => new Map(catalog.map(card => [card.id, card])),
    [catalog],
  );

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

  const loadCollection = useCallback(async () => {
    const [catalogRes, ownedRes, decksRes] = await Promise.all([
      getCardCatalog(),
      getMyCards(),
      getDecks(),
    ]);
    setCatalog(catalogRes.cards);
    setOwnedCards(ownedRes.cards);
    setDecks(decksRes.decks);
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
      .then(async () => {
        await loadProfile();
        await loadCollection();
        return restoreCurrentRoom();
      })
      .then(restored => {
        if (!restored) setView('profile');
      })
      .catch(() => setView('login'));
  }, [loadCollection, loadProfile, restoreCurrentRoom]);

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
          <div className="subtitle">Collection, deckbuilding and PvP room client</div>
        </div>
        <nav>
          {profile && (
            <>
              <button onClick={() => setView('profile')}>Profile</button>
              <button onClick={() => setView('cards')}>Collection</button>
              <button onClick={() => setView('decks')}>Decks</button>
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
            await loadCollection();
            const restored = await restoreCurrentRoom();
            if (!restored) setView('profile');
          }}
        />
      )}

      {view === 'profile' && profile && (
        <ProfileScreen
          profile={profile}
          decks={decks}
          onOpenCards={() => setView('cards')}
          onOpenDecks={() => setView('decks')}
          onFindMatch={() => setView('queue')}
        />
      )}

      {view === 'cards' && (
        <CollectionScreen catalog={catalog} ownedCards={ownedCards} cardsById={cardsById} />
      )}

      {view === 'decks' && (
        <DecksScreen
          decks={decks}
          cardsById={cardsById}
          onCreate={() => {
            setDeckEditor({ mode: 'create' });
            setView('deck-edit');
          }}
          onEdit={deckId => {
            setDeckEditor({ mode: 'edit', deckId });
            setView('deck-edit');
          }}
        />
      )}

      {view === 'deck-edit' && deckEditor && (
        <DeckEditorScreen
          editor={deckEditor}
          catalog={catalog}
          ownedCards={ownedCards}
          cardsById={cardsById}
          onCancel={() => setView('decks')}
          onSaved={async message => {
            await loadCollection();
            showNotice(message);
            setView('decks');
          }}
        />
      )}

      {view === 'queue' && (
        <QueueScreen
          decks={decks}
          cardsById={cardsById}
          onDecksChanged={loadCollection}
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
          cardsById={cardsById}
          onFinished={() => setView('result')}
          onNotice={showNotice}
        />
      )}

      {view === 'result' && battle && (
        <ResultScreen
          battle={battle}
          selfId={profile?.user_id ?? -1}
          onDone={() => {
            setBattle(null);
            setView('profile');
          }}
        />
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
      await onDone(typeof result === 'string' ? result : 'success');
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

function ProfileScreen({
  profile,
  decks,
  onOpenCards,
  onOpenDecks,
  onFindMatch,
}: {
  profile: Profile;
  decks: DeckSummary[];
  onOpenCards: () => void;
  onOpenDecks: () => void;
  onFindMatch: () => void;
}) {
  const stats = profile.stats ?? {};
  const safeDecks = Array.isArray(decks) ? decks : [];
  const activeDecks = safeDecks.filter(deck => deck.is_active).length;

  return (
    <main className="page-grid">
      <section className="profile-card">
        <div className="avatar-ring">{profile.username.slice(0, 2).toUpperCase()}</div>
        <div>
          <div className="panel-kicker">Player Profile</div>
          <h2>{profile.username}</h2>
          <p>User #{profile.user_id}</p>
        </div>
        <div className="profile-actions">
          <button className="primary-action" onClick={onFindMatch}>
            Find Match
          </button>
          <button onClick={onOpenDecks}>Manage Decks</button>
        </div>
      </section>

      <section className="stat-grid">
        <Metric label="Wins" value={stats.wins ?? 0} />
        <Metric label="Losses" value={stats.losses ?? 0} />
        <Metric label="Draws" value={stats.draws ?? 0} />
        <Metric label="Matches" value={stats.total_matches ?? 0} />
      </section>

      <section className="wide-panel split-panel">
        <div>
          <div className="section-title">Collection Overview</div>
          <p className="panel-copy">浏览已拥有卡牌，按当前规则构建 30 张卡组，然后带着有效卡组进入匹配。</p>
        </div>
        <div className="summary-strip">
          <div className="summary-chip">
            <span>Decks</span>
            <strong>{safeDecks.length}</strong>
          </div>
          <div className="summary-chip">
            <span>Active</span>
            <strong>{activeDecks}</strong>
          </div>
          <button onClick={onOpenCards}>Open Collection</button>
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

function CollectionScreen({
  catalog,
  ownedCards,
  cardsById,
}: {
  catalog: CardCatalogItem[];
  ownedCards: OwnedCard[];
  cardsById: Map<number, CardCatalogItem>;
}) {
  const ownedById = useMemo(
    () => new Map(ownedCards.map(card => [card.id, card.quantity])),
    [ownedCards],
  );
  const collection = useMemo(() => {
    return ownedCards
      .map(entry => ({
        ...cardsById.get(entry.id),
        owned: entry.quantity,
      }))
      .filter((card): card is CardCatalogItem & { owned: number } => Boolean(card?.id))
      .sort((left, right) => left.mana_cost - right.mana_cost || left.id - right.id);
  }, [cardsById, ownedCards]);

  return (
    <main className="page-grid single-column">
      <section className="wide-panel split-panel">
        <div>
          <div className="section-title">My Collection</div>
          <p className="panel-copy">后端 `cards/my` 只返回拥有数量，这里按 `cards/catalog` 做卡牌元数据关联。</p>
        </div>
        <div className="summary-strip">
          <div className="summary-chip">
            <span>Owned Types</span>
            <strong>{ownedCards.length}</strong>
          </div>
          <div className="summary-chip">
            <span>Catalog</span>
            <strong>{catalog.length}</strong>
          </div>
        </div>
      </section>

      <section className="card-grid">
        {collection.map(card => (
          <article className="card-tile" key={card.id}>
            <CardArt cardId={card.id} name={card.name} className="card-art collection-art" />
            <div className="card-head">
              <span className="mana-gem">{card.mana_cost}</span>
              <div>
                <strong>{card.name}</strong>
                <span>
                  #{card.id} · {card.card_type}
                </span>
              </div>
              <span className="owned-pill">x{ownedById.get(card.id) ?? 0}</span>
            </div>
            <p>{card.description || 'No description'}</p>
            <div className="card-foot">
              <span>{card.class_type || 'neutral'}</span>
              <span>
                {card.attack ?? '-'} / {card.health ?? '-'}
              </span>
            </div>
          </article>
        ))}
      </section>
    </main>
  );
}

function DecksScreen({
  decks,
  cardsById,
  onCreate,
  onEdit,
}: {
  decks: DeckSummary[];
  cardsById: Map<number, CardCatalogItem>;
  onCreate: () => void;
  onEdit: (deckId: number) => void;
}) {
  const [expandedDeck, setExpandedDeck] = useState<number | null>(null);
  const [deckDetails, setDeckDetails] = useState<Record<number, DeckEntry[]>>({});
  const [busyDeck, setBusyDeck] = useState<number | null>(null);
  const safeDecks = Array.isArray(decks) ? decks : [];

  async function toggleDeck(deckId: number) {
    if (expandedDeck === deckId) {
      setExpandedDeck(null);
      return;
    }
    if (!deckDetails[deckId]) {
      setBusyDeck(deckId);
      try {
        const detail = await getDeck(deckId);
        setDeckDetails(current => ({ ...current, [deckId]: detail.cards }));
      } finally {
        setBusyDeck(null);
      }
    }
    setExpandedDeck(deckId);
  }

  return (
    <main className="page-grid single-column">
      <section className="wide-panel split-panel">
        <div>
          <div className="section-title">Deck Workshop</div>
          <p className="panel-copy">有效卡组才能进入匹配。当前后端规则是 30 张牌，单卡数量不超过 2，且必须拥有这些牌。</p>
        </div>
        <button className="primary-action" onClick={onCreate}>
          Create Deck
        </button>
      </section>

      <section className="deck-list">
        {safeDecks.map(deck => {
          const details = deckDetails[deck.deck_id] ?? [];
          return (
            <article className={`deck-row ${deck.is_active ? 'active' : 'inactive'}`} key={deck.deck_id}>
              <div className="deck-meta">
                <strong>{deck.name}</strong>
                <span>
                  deck #{deck.deck_id} · {deck.is_active ? 'active' : 'incomplete'}
                </span>
              </div>
              <div className="deck-tools">
                <button onClick={() => toggleDeck(deck.deck_id)}>
                  {busyDeck === deck.deck_id ? 'Loading...' : expandedDeck === deck.deck_id ? 'Hide' : 'Preview'}
                </button>
                <button onClick={() => onEdit(deck.deck_id)}>Edit</button>
              </div>
              {expandedDeck === deck.deck_id && (
                <div className="deck-preview">
                  {details.length === 0 && <span className="subtitle">No cards loaded</span>}
              {details.map(entry => (
                <div className="deck-preview-row" key={`${deck.deck_id}-${entry.card_id}`}>
                  <CardArt
                    cardId={entry.card_id}
                    name={cardName(cardsById, entry.card_id)}
                    className="card-art preview-art"
                  />
                  <span>{cardName(cardsById, entry.card_id)}</span>
                  <strong>x{entry.quantity}</strong>
                </div>
              ))}
                </div>
              )}
            </article>
          );
        })}
      </section>
    </main>
  );
}

function DeckEditorScreen({
  editor,
  catalog,
  ownedCards,
  cardsById,
  onCancel,
  onSaved,
}: {
  editor: DeckEditorState;
  catalog: CardCatalogItem[];
  ownedCards: OwnedCard[];
  cardsById: Map<number, CardCatalogItem>;
  onCancel: () => void;
  onSaved: (message: string) => void;
}) {
  const [name, setName] = useState(editor.mode === 'create' ? 'New Deck' : '');
  const [entries, setEntries] = useState<Record<number, number>>({});
  const [busy, setBusy] = useState(editor.mode === 'edit');
  const [error, setError] = useState('');

  const ownedById = useMemo(
    () => new Map(ownedCards.map(card => [card.id, card.quantity])),
    [ownedCards],
  );

  useEffect(() => {
    if (editor.mode !== 'edit' || editor.deckId == null) return;
    setBusy(true);
    getDeck(editor.deckId)
      .then(detail => {
        setName(detail.name);
        const next: Record<number, number> = {};
        detail.cards.forEach(entry => {
          next[entry.card_id] = entry.quantity;
        });
        setEntries(next);
      })
      .catch(err => setError(err instanceof Error ? err.message : 'load deck failed'))
      .finally(() => setBusy(false));
  }, [editor]);

  const editableCards = useMemo(() => {
    return ownedCards
      .map(card => cardsById.get(card.id))
      .filter((card): card is CardCatalogItem => Boolean(card?.id))
      .sort((left, right) => left.mana_cost - right.mana_cost || left.id - right.id);
  }, [cardsById, ownedCards]);

  const deckEntries = useMemo(() => {
    return Object.entries(entries)
      .map(([cardId, quantity]) => ({ card_id: Number(cardId), quantity }))
      .filter(entry => entry.quantity > 0);
  }, [entries]);

  const total = cardCount(deckEntries);

  async function submit() {
    setBusy(true);
    setError('');
    try {
      const result =
        editor.mode === 'create'
          ? await createDeck(name, deckEntries)
          : await updateDeck(editor.deckId!, name, deckEntries);
      if (result.state !== 'SUCCESS') {
        throw new Error(result.message ?? 'save failed');
      }
      onSaved(result.incomplete ? 'Deck saved as incomplete' : 'Deck saved');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'save failed');
    } finally {
      setBusy(false);
    }
  }

  return (
    <main className="page-grid single-column">
      <section className="wide-panel">
        <div className="editor-head">
          <div>
            <div className="section-title">{editor.mode === 'create' ? 'Create Deck' : 'Edit Deck'}</div>
            <p className="panel-copy">构建请求会直接发送到 `POST /decks/` 或 `POST /decks/{'{id}'}`。</p>
          </div>
          <div className="summary-strip">
            <div className="summary-chip">
              <span>Total</span>
              <strong>{total}/30</strong>
            </div>
          </div>
        </div>
        <label>
          Deck Name
          <input value={name} onChange={event => setName(event.target.value)} />
        </label>
        {error && <div className="error-line">{error}</div>}
      </section>

      <section className="editor-grid">
        <div className="wide-panel">
          <div className="section-title">Owned Cards</div>
          <div className="editor-card-list">
            {editableCards.map(card => {
              const owned = ownedById.get(card.id) ?? 0;
              const value = entries[card.id] ?? 0;
              return (
                <div className="editor-card-row" key={card.id}>
                  <div>
                    <strong>{card.name}</strong>
                    <span>
                      #{card.id} · mana {card.mana_cost} · own {owned}
                    </span>
                  </div>
                  <input
                    className="qty-input"
                    type="number"
                    min={0}
                    max={owned}
                    value={value}
                    onChange={event =>
                      setEntries(current => ({
                        ...current,
                        [card.id]: Math.max(0, Math.min(owned, Number(event.target.value) || 0)),
                      }))
                    }
                  />
                </div>
              );
            })}
          </div>
        </div>

        <div className="wide-panel">
          <div className="section-title">Current Deck</div>
          <div className="deck-preview compact">
            {deckEntries.length === 0 && <span className="subtitle">No cards selected</span>}
            {deckEntries.map(entry => (
              <div className="deck-preview-row" key={entry.card_id}>
                <CardArt
                  cardId={entry.card_id}
                  name={cardName(cardsById, entry.card_id)}
                  className="card-art preview-art"
                />
                <span>{cardName(cardsById, entry.card_id)}</span>
                <strong>x{entry.quantity}</strong>
              </div>
            ))}
          </div>
          <div className="editor-actions">
            <button className="primary-action" disabled={busy} onClick={submit}>
              {busy ? 'Saving...' : 'Save Deck'}
            </button>
            <button disabled={busy} onClick={onCancel}>
              Cancel
            </button>
          </div>
        </div>
      </section>
    </main>
  );
}

function QueueScreen({
  decks,
  cardsById,
  onDecksChanged,
  onMatched,
  onNotice,
}: {
  decks: DeckSummary[];
  cardsById: Map<number, CardCatalogItem>;
  onDecksChanged: () => Promise<void>;
  onMatched: (match: Awaited<ReturnType<typeof joinMatch>>) => void;
  onNotice: (message: string) => void;
}) {
  const [queueing, setQueueing] = useState(false);
  const [status, setStatus] = useState('Ready');
  const [selectedDeckId, setSelectedDeckId] = useState<number | null>(null);
  const pollTimer = useRef<number | null>(null);
  const activeDecks = useMemo(() => decks.filter(deck => deck.is_active), [decks]);

  useEffect(() => {
    if (activeDecks.length > 0 && selectedDeckId == null) {
      setSelectedDeckId(activeDecks[0].deck_id);
    }
  }, [activeDecks, selectedDeckId]);

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
      pollTimer.current = window.setTimeout(poll, 150);
    } catch (err) {
      setStatus(err instanceof Error ? err.message : 'poll failed');
      setQueueing(false);
    }
  }, [onMatched]);

  async function start() {
    if (selectedDeckId == null) {
      onNotice('Select an active deck first');
      return;
    }
    setQueueing(true);
    setStatus('Joining queue');
    try {
      const result = await joinMatch(selectedDeckId);
      if (result.status === 'MATCHED') {
        onMatched(result);
        return;
      }
      setStatus(result.message ?? 'Waiting for opponent');
      poll();
    } catch (err) {
      setQueueing(false);
      setStatus(err instanceof Error ? err.message : 'join failed');
      await onDecksChanged();
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
        <div className="deck-list">
          {decks.map(deck => (
            <button
              className={`deck-choice ${selectedDeckId === deck.deck_id ? 'selected' : ''}`}
              disabled={!deck.is_active || queueing}
              key={deck.deck_id}
              onClick={() => setSelectedDeckId(deck.deck_id)}
            >
              <div>
                <strong>{deck.name}</strong>
                <span>
                  #{deck.deck_id} · {deck.is_active ? 'active' : 'incomplete'}
                </span>
              </div>
              <span>{deck.class_type || 'unused'}</span>
            </button>
          ))}
        </div>
        {selectedDeckId != null && (
          <SelectedDeckPreview deckId={selectedDeckId} cardsById={cardsById} />
        )}
      </section>
      <aside className="queue-panel">
        <div className="status-orb" />
        <h2>{status}</h2>
        <p>匹配结果通过轮询获取；进入房间后，操作与事件只使用 WebSocket。</p>
        <button className="primary-action" disabled={queueing || selectedDeckId == null} onClick={start}>
          Find Match
        </button>
        <button disabled={!queueing} onClick={cancel}>
          Cancel
        </button>
      </aside>
    </main>
  );
}

function SelectedDeckPreview({
  deckId,
  cardsById,
}: {
  deckId: number;
  cardsById: Map<number, CardCatalogItem>;
}) {
  const [cards, setCards] = useState<DeckEntry[]>([]);

  useEffect(() => {
    getDeck(deckId).then(detail => setCards(detail.cards)).catch(() => setCards([]));
  }, [deckId]);

  return (
    <div className="deck-preview">
      {cards.map(entry => (
        <div className="deck-preview-row" key={`${deckId}-${entry.card_id}`}>
          <CardArt
            cardId={entry.card_id}
            name={cardName(cardsById, entry.card_id)}
            className="card-art preview-art"
          />
          <span>{cardName(cardsById, entry.card_id)}</span>
          <strong>x{entry.quantity}</strong>
        </div>
      ))}
    </div>
  );
}

function BattleRoom({
  battle,
  selfId,
  cardsById,
  onFinished,
  onNotice,
}: {
  battle: BattleContext;
  selfId: number;
  cardsById: Map<number, CardCatalogItem>;
  onFinished: () => void;
  onNotice: (message: string) => void;
}) {
  const [snapshot, setSnapshot] = useState<RoomSnapshot | null>(null);
  const [events, setEvents] = useState<RoomEvent[]>([]);
  const [sequence, setSequence] = useState(0);
  const [version, setVersion] = useState(0);
  const [selectedAction, setSelectedAction] = useState<SelectedAction | null>(null);
  const [busy, setBusy] = useState(false);
  const requestCounter = useRef(1);
  const sequenceRef = useRef(0);
  const versionRef = useRef(0);
  const socketRef = useRef<WebSocket | null>(null);
  const pendingOperations = useRef(new Map<
    number,
    { resolve: (result: OperationResult) => void; reject: (error: Error) => void }
  >());

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
    let active = true;
    let socket: WebSocket | null = null;
    let reconnectTimer: number | undefined;
    let processing = Promise.resolve();

    async function processEvents(
      newEvents: RoomEvent[],
      pushedSnapshot?: RoomSnapshot,
      applyIncrementally = false,
    ) {
      if (!active) return;
      if (newEvents.some(event => event.type === 'error_require_snapshot')) {
        await refreshSnapshot();
        return;
      }
      const nextSequence = Math.max(
        sequenceRef.current,
        pushedSnapshot?.last_sequence ?? 0,
        ...newEvents.map(event => event.sequence),
      );
      const nextVersion = Math.max(
        versionRef.current,
        pushedSnapshot?.version ?? 0,
        ...newEvents.map(event => event.room_version),
      );
      sequenceRef.current = nextSequence;
      versionRef.current = nextVersion;
      if (newEvents.length > 0) setEvents(current => mergeEvents(current, newEvents));
      setSequence(nextSequence);
      setVersion(nextVersion);
      if (pushedSnapshot) setSnapshot(pushedSnapshot);
      else if (applyIncrementally && newEvents.length > 0) {
        setSnapshot(current => current
          ? applyRoomEvents(current, newEvents, selfId, cardsById)
          : current);
      }
      else if (newEvents.length > 0) await refreshSnapshot();
      if (newEvents.some(event => event.type === 'game_end')) onFinished();
    }

    function enqueueEvents(
      newEvents: RoomEvent[],
      pushedSnapshot?: RoomSnapshot,
      applyIncrementally = false,
    ) {
      processing = processing
        .then(() => processEvents(newEvents, pushedSnapshot, applyIncrementally))
        .catch(err => onNotice(err instanceof Error ? err.message : 'event sync failed'));
    }

    function connectWebSocket() {
      if (!active) return;
      let currentSocket: WebSocket;
      try {
        currentSocket = new WebSocket(roomWebSocketUrl(battle.roomId, sequenceRef.current));
        socket = currentSocket;
        socketRef.current = currentSocket;
      } catch {
        reconnectTimer = window.setTimeout(connectWebSocket, 4000);
        return;
      }

      currentSocket.onmessage = event => {
        try {
          const result = JSON.parse(event.data) as [
            number, number, number, number, RoomEventBatch?,
          ] | {
            events?: RoomEvent[];
            batch?: RoomEventBatch;
            snapshot?: RoomSnapshot;
          };
          if (Array.isArray(result)) {
            if ((result.length !== 4 && result.length !== 5) || result[0] !== 2) {
              throw new Error('invalid response');
            }
            const [, requestId, errorCode, responseVersion, batch] = result;
            const pending = pendingOperations.current.get(requestId);
            if (pending) {
              pendingOperations.current.delete(requestId);
              const actionErrors = [
                '',
                'PlayerNotInRoom',
                'RoomFinished',
                'NotYourTurn',
                'InvalidCard',
                'InvalidTarget',
                'InsufficientMana',
                'BoardFull',
                'StaleVersion',
              ];
              pending.resolve(errorCode === 0
                ? { state: 'SUCCESS', version: responseVersion }
                : {
                    state: 'FAIL',
                    version: responseVersion,
                    action_error: actionErrors[errorCode] ?? 'UnknownError',
                  });
            }
            if (batch) enqueueEvents(expandEventBatch(batch), undefined, true);
            return;
          }
          enqueueEvents(
            result.batch ? expandEventBatch(result.batch) : result.events ?? [],
            result.snapshot,
            true,
          );
        } catch {
          currentSocket.close();
        }
      };
      currentSocket.onerror = () => currentSocket.close();
      currentSocket.onclose = () => {
        if (socketRef.current === currentSocket) socketRef.current = null;
        for (const pending of pendingOperations.current.values()) {
          pending.reject(new Error('WebSocket disconnected; reconnecting'));
        }
        pendingOperations.current.clear();
        if (!active) return;
        reconnectTimer = window.setTimeout(connectWebSocket, 4000);
      };
    }

    connectWebSocket();
    return () => {
      active = false;
      if (reconnectTimer != null) window.clearTimeout(reconnectTimer);
      if (socketRef.current === socket) socketRef.current = null;
      socket?.close();
    };
  }, [battle.roomId, cardsById, onFinished, onNotice, refreshSnapshot, selfId, snapshot !== null]);

  const players = useMemo(() => {
    if (!snapshot) return null;
    const first = snapshot.player_0;
    const second = snapshot.player_1;
    return first.user_id === selfId ? { you: first, opponent: second } : { you: second, opponent: first };
  }, [selfId, snapshot]);

  useEffect(() => {
    if (!selectedAction || !snapshot || !players) return;
    const cards = selectedAction.type === 'play card' ? snapshot.my_hand : players.you.board;
    if (!cards.some(card => card.instance_id === selectedAction.instanceId)) {
      setSelectedAction(null);
    }
  }, [players, selectedAction, snapshot]);

  async function operate(
    type: 'play card' | 'attack' | 'end turn' | 'surrender',
    cardInstance = 0,
    target = -1,
    targetType: 'minion' | 'hero' = 'minion',
  ) {
    if (!snapshot || busy) return;
    setBusy(true);
    try {
      const socket = socketRef.current;
      if (!socket || socket.readyState !== WebSocket.OPEN) {
        throw new Error('WebSocket is reconnecting');
      }
      const requestId = requestCounter.current++;
      const operationTypes = {
        'play card': 0,
        attack: 1,
        'end turn': 2,
        surrender: 3,
      } as const;
      const result = await new Promise<OperationResult>((resolve, reject) => {
        pendingOperations.current.set(requestId, { resolve, reject });
        try {
          socket.send(JSON.stringify([
            1,
            requestId,
            versionRef.current,
            operationTypes[type],
            cardInstance,
            target,
            targetType === 'hero' ? 1 : 0,
          ]));
        } catch (error) {
          pendingOperations.current.delete(requestId);
          reject(error instanceof Error ? error : new Error('operation send failed'));
        }
      });
      if (result.state !== 'SUCCESS') {
        onNotice(result.action_error ?? result.message ?? 'operation failed');
        return;
      }
      setVersion(result.version ?? versionRef.current);
      versionRef.current = result.version ?? versionRef.current;
      setSelectedAction(null);
    } catch (err) {
      onNotice(err instanceof Error ? err.message : 'operation failed');
    } finally {
      setBusy(false);
    }
  }

  if (!snapshot || !players) {
    return <main className="loading-panel">Loading room {battle.roomId}...</main>;
  }

  const isMyTurn = snapshot.current_player === selfId;
  const selectHandCard = (card: SnapshotCardRef) => {
    if (!isMyTurn || busy) return;
    const definition = card.card_id ? cardsById.get(card.card_id) : undefined;
    setSelectedAction({
      type: 'play card',
      instanceId: card.instance_id,
      requiresTarget: hasSelectedBattlecry(definition),
    });
  };
  const selectAttacker = (instanceId: number) => {
    setSelectedAction({ type: 'attack', instanceId, requiresTarget: true });
  };
  const chooseTarget = (targetId: number, targetType: 'minion' | 'hero') => {
    if (!selectedAction || !isMyTurn || busy) return;
    void operate(selectedAction.type, selectedAction.instanceId, targetId, targetType);
  };
  const playSelectedCard = () => {
    if (selectedAction?.type !== 'play card') return;
    if (selectedAction.requiresTarget) {
      onNotice('请将卡牌拖到目标上，或先选择卡牌再点击目标');
      return;
    }
    void operate('play card', selectedAction.instanceId);
  };
  const targetingWithCard = Boolean(
    isMyTurn && !busy && selectedAction?.type === 'play card' && selectedAction.requiresTarget,
  );
  const targetingWithAttack = Boolean(
    isMyTurn && !busy && selectedAction?.type === 'attack',
  );
  const playingWithoutTarget = Boolean(
    isMyTurn && !busy && selectedAction?.type === 'play card' && !selectedAction.requiresTarget,
  );

  return (
    <main className="battle-shell">
      <section className="room-header">
        <span>Room {battle.roomId}</span>
        <span>Match {battle.matchId}</span>
        <span>{isMyTurn ? 'Your turn' : 'Opponent turn'}</span>
      </section>

      <PlayerStrip
        label="Opponent"
        player={players.opponent}
        targetable={targetingWithCard || targetingWithAttack}
        onTarget={() => chooseTarget(players.opponent.user_id, 'hero')}
      />

      <section className="board">
        <div className="opponent-hand">
          {Array.from({ length: players.opponent.hand_count }, (_, index) => (
            <div className="card-back" key={index} />
          ))}
        </div>
        <BoardRow
          cards={players.opponent.board}
          cardsById={cardsById}
          selectedAction={selectedAction}
          targetable={targetingWithCard || targetingWithAttack}
          onTarget={instanceId => chooseTarget(instanceId, 'minion')}
        />
        <BoardRow
          cards={players.you.board}
          cardsById={cardsById}
          active
          canSelectAttacker={isMyTurn && !busy}
          selectedAction={selectedAction}
          targetable={targetingWithCard}
          playDropEnabled={playingWithoutTarget}
          onSelectAttacker={selectAttacker}
          onTarget={instanceId => chooseTarget(instanceId, 'minion')}
          onPlayDrop={playSelectedCard}
        />
      </section>

      <section className="hand-row">
        {snapshot.my_hand.map(card => {
          const definition = card.card_id ? cardsById.get(card.card_id) : undefined;
          return (
            <button
              className={`hand-card ${selectedAction?.type === 'play card' && selectedAction.instanceId === card.instance_id ? 'selected' : ''}`}
              key={card.instance_id}
              draggable={isMyTurn && !busy}
              onClick={() => selectHandCard(card)}
              onDragStart={event => {
                selectHandCard(card);
                event.dataTransfer.effectAllowed = 'move';
                event.dataTransfer.setData('text/plain', String(card.instance_id));
              }}
            >
              <CardArt
                cardId={card.card_id}
                name={cardName(cardsById, card.card_id)}
                className="card-art hand-art"
              />
              <span className="mana-stat mana-corner">{definition?.mana_cost ?? '-'}</span>
              <span>{cardName(cardsById, card.card_id)}</span>
              <div className="battle-card-stats">
                <span className="attack-stat">{definition?.attack ?? '-'}</span>
                <span className="health-stat">{definition?.health ?? '-'}</span>
              </div>
              {definition?.description && (
                <span className="card-description">{definition.description}</span>
              )}
            </button>
          );
        })}
      </section>

      <PlayerStrip
        label="You"
        player={players.you}
        targetable={targetingWithCard}
        onTarget={() => chooseTarget(players.you.user_id, 'hero')}
      />

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
        <div className="mana-block">
          <span>Version</span>
          <strong>{version}</strong>
        </div>
        <button
          className="primary-action"
          disabled={busy || selectedAction?.type !== 'play card' || !isMyTurn}
          onClick={playSelectedCard}
        >
          {targetingWithCard ? 'Choose Target' : 'Play Card'}
        </button>
        <button disabled={busy || !isMyTurn} onClick={() => void operate('end turn')}>
          End Turn
        </button>
        <button disabled={busy} onClick={() => void operate('surrender')}>
          Surrender
        </button>
        <div className="event-log">
          <div className="section-title">Events</div>
          {events.length === 0 && <p>No events yet</p>}
          {events.map(event => (
            <div className="event-line" key={eventKey(event)}>
              #{event.sequence} {formatEvent(cardsById, event)}
            </div>
          ))}
        </div>
      </aside>
    </main>
  );
}

function PlayerStrip({
  label,
  player,
  targetable = false,
  onTarget,
}: {
  label: string;
  player: RoomSnapshot['player_0'];
  targetable?: boolean;
  onTarget?: () => void;
}) {
  return (
    <section
      className={`player-strip ${targetable ? 'targetable' : ''}`}
      role={targetable ? 'button' : undefined}
      tabIndex={targetable ? 0 : undefined}
      onClick={targetable ? onTarget : undefined}
      onKeyDown={event => {
        if (targetable && (event.key === 'Enter' || event.key === ' ')) onTarget?.();
      }}
      onDragOver={event => {
        if (targetable) event.preventDefault();
      }}
      onDrop={event => {
        if (!targetable) return;
        event.preventDefault();
        onTarget?.();
      }}
    >
      <div className="avatar-ring small">{label.slice(0, 1)}</div>
      <div>
        <strong>{label}</strong>
        <span>Player #{player.user_id}</span>
      </div>
      <div className="player-stats">
        <span>Deck {player.deck_count}</span>
        <span>Hand {player.hand_count}</span>
      </div>
      <div className="health-pill">{player.health}</div>
    </section>
  );
}

function BoardRow({
  cards,
  cardsById,
  active = false,
  canSelectAttacker = false,
  selectedAction,
  targetable = false,
  playDropEnabled = false,
  onSelectAttacker,
  onTarget,
  onPlayDrop,
}: {
  cards: SnapshotCardRef[];
  cardsById: Map<number, CardCatalogItem>;
  active?: boolean;
  canSelectAttacker?: boolean;
  selectedAction: SelectedAction | null;
  targetable?: boolean;
  playDropEnabled?: boolean;
  onSelectAttacker?: (instanceId: number) => void;
  onTarget: (instanceId: number) => void;
  onPlayDrop?: () => void;
}) {
  return (
    <div
      className={`board-row ${active ? 'active' : ''} ${playDropEnabled ? 'play-drop-target' : ''}`}
      onDragOver={event => {
        if (playDropEnabled) event.preventDefault();
      }}
      onDrop={event => {
        if (!playDropEnabled) return;
        event.preventDefault();
        onPlayDrop?.();
      }}
    >
      {Array.from({ length: 7 }, (_, index) => {
        const card = cards[index];
        const definition = card?.card_id ? cardsById.get(card.card_id) : undefined;
        const canAttack = Boolean(
          card && canSelectAttacker && !card.exhausted && (card.attack ?? 0) > 0,
        );
        return (
          <div
            className={`board-slot ${card && targetable ? 'targetable' : ''} ${card && selectedAction?.type === 'attack' && selectedAction.instanceId === card.instance_id ? 'selected' : ''} ${card?.exhausted ? 'exhausted' : ''}`}
            key={index}
            draggable={canAttack}
            onClick={() => {
              if (!card) return;
              if (targetable) onTarget(card.instance_id);
              else if (canAttack) onSelectAttacker?.(card.instance_id);
            }}
            onDragStart={event => {
              if (!card || !canAttack) return;
              onSelectAttacker?.(card.instance_id);
              event.dataTransfer.effectAllowed = 'move';
              event.dataTransfer.setData('text/plain', String(card.instance_id));
            }}
            onDragOver={event => {
              if (card && targetable) event.preventDefault();
            }}
            onDrop={event => {
              if (!card || !targetable) return;
              event.preventDefault();
              onTarget(card.instance_id);
            }}
          >
            {card ? (
              <>
                <CardArt
                  cardId={card.card_id}
                  name={cardName(cardsById, card.card_id)}
                  className="card-art board-art"
                />
                <strong>{cardName(cardsById, card.card_id)}</strong>
                <div className="battle-card-stats">
                  <span className="attack-stat">{card.attack ?? definition?.attack ?? '-'}</span>
                  <span className="health-stat">{card.health ?? definition?.health ?? '-'}</span>
                </div>
                {definition?.description && (
                  <span className="card-description">{definition.description}</span>
                )}
              </>
            ) : (
              ''
            )}
          </div>
        );
      })}
    </div>
  );
}

function CardArt({
  cardId,
  name,
  className,
}: {
  cardId?: number;
  name: string;
  className: string;
}) {
  return <img className={className} src={cardArtUrl(cardId)} alt={name} loading="lazy" />;
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
