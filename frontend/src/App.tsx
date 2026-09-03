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
  pollRoom,
  register,
  sendOperation,
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
        <p>当前使用匹配长轮询，房间内使用 long poll 事件流。</p>
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
  const [selectedCard, setSelectedCard] = useState<number>(0);
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

    async function loop() {
      while (polling.current) {
        try {
          const result = await pollRoom(battle.roomId, sequenceRef.current);
          const newEvents = result.events ?? [];
          if (newEvents.some(event => event.type === 'error_require_snapshot')) {
            await refreshSnapshot();
            continue;
          }
          if (newEvents.length > 0) {
            const nextSequence = Math.max(sequenceRef.current, ...newEvents.map(event => event.sequence));
            const nextVersion = Math.max(versionRef.current, ...newEvents.map(event => event.room_version));
            sequenceRef.current = nextSequence;
            versionRef.current = nextVersion;
            setEvents(current => mergeEvents(current, newEvents));
            setSequence(nextSequence);
            setVersion(nextVersion);
            await refreshSnapshot();
            if (newEvents.some(event => event.type === 'game_end')) {
              onFinished();
              return;
            }
          }
        } catch (err) {
          if (!polling.current) return;
          onNotice(err instanceof Error ? err.message : 'room poll failed');
          await new Promise(resolve => window.setTimeout(resolve, 1200));
        }
      }
    }

    loop();
    return () => {
      polling.current = false;
    };
  }, [battle.roomId, onFinished, onNotice, refreshSnapshot, snapshot]);

  const players = useMemo(() => {
    if (!snapshot) return null;
    const first = snapshot.player_0;
    const second = snapshot.player_1;
    return first.user_id === selfId ? { you: first, opponent: second } : { you: second, opponent: first };
  }, [selfId, snapshot]);

  useEffect(() => {
    if (!snapshot?.my_hand.length) {
      setSelectedCard(0);
      return;
    }
    if (!snapshot.my_hand.some(card => card.instance_id === selectedCard)) {
      setSelectedCard(snapshot.my_hand[0].instance_id);
    }
  }, [selectedCard, snapshot]);

  async function operate(type: 'play card' | 'end turn' | 'surrender') {
    if (!snapshot || busy) return;
    setBusy(true);
    try {
      const result: OperationResult = await sendOperation(
        battle.roomId,
        type,
        versionRef.current,
        requestCounter.current++,
        type === 'play card' ? selectedCard : 0,
      );
      if (result.state !== 'SUCCESS') {
        onNotice(result.action_error ?? result.message ?? 'operation failed');
        return;
      }
      setVersion(result.version ?? versionRef.current);
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

  const isMyTurn = snapshot.current_player === selfId;

  return (
    <main className="battle-shell">
      <section className="room-header">
        <span>Room {battle.roomId}</span>
        <span>Match {battle.matchId}</span>
        <span>{isMyTurn ? 'Your turn' : 'Opponent turn'}</span>
      </section>

      <PlayerStrip label="Opponent" player={players.opponent} />

      <section className="board">
        <div className="opponent-hand">
          {Array.from({ length: players.opponent.hand_count }, (_, index) => (
            <div className="card-back" key={index} />
          ))}
        </div>
        <BoardRow cards={players.opponent.board} cardsById={cardsById} />
        <BoardRow cards={players.you.board} cardsById={cardsById} active />
      </section>

      <section className="hand-row">
        {snapshot.my_hand.map(card => (
          <button
            className={`hand-card ${selectedCard === card.instance_id ? 'selected' : ''}`}
            key={card.instance_id}
            onClick={() => setSelectedCard(card.instance_id)}
          >
            <CardArt
              cardId={card.card_id}
              name={cardName(cardsById, card.card_id)}
              className="card-art hand-art"
            />
            <span>{cardName(cardsById, card.card_id)}</span>
            <small>instance {card.instance_id}</small>
            <small>card {card.card_id ?? '?'}</small>
          </button>
        ))}
      </section>

      <PlayerStrip label="You" player={players.you} />

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
          disabled={busy || snapshot.my_hand.length === 0 || !isMyTurn}
          onClick={() => operate('play card')}
        >
          Play Card
        </button>
        <button disabled={busy || !isMyTurn} onClick={() => operate('end turn')}>
          End Turn
        </button>
        <button disabled={busy} onClick={() => operate('surrender')}>
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
}: {
  label: string;
  player: RoomSnapshot['player_0'];
}) {
  return (
    <section className="player-strip">
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
}: {
  cards: SnapshotCardRef[];
  cardsById: Map<number, CardCatalogItem>;
  active?: boolean;
}) {
  return (
    <div className={`board-row ${active ? 'active' : ''}`}>
      {Array.from({ length: 7 }, (_, index) => {
        const card = cards[index];
        return (
          <div className="board-slot" key={index}>
            {card ? (
              <>
                <CardArt
                  cardId={card.card_id}
                  name={cardName(cardsById, card.card_id)}
                  className="card-art board-art"
                />
                <strong>{cardName(cardsById, card.card_id)}</strong>
                <span>#{card.instance_id}</span>
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
