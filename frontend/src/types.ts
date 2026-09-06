export type View =
  | 'login'
  | 'profile'
  | 'cards'
  | 'decks'
  | 'deck-edit'
  | 'queue'
  | 'room'
  | 'result';

export type Profile = {
  user_id: number;
  username: string;
  stats?: {
    wins?: number;
    losses?: number;
    draws?: number;
    total_matches?: number;
  };
};

export type MatchResponse = {
  status: 'WAITING' | 'MATCHED' | 'FAIL' | 'CANCELLED';
  message?: string;
  match_id?: number;
  opponent_id?: number;
  room_id?: number;
};

export type RoomEvent = {
  actor_id: number;
  card_id?: number;
  card_instance?: number;
  target_id?: number;
  target_type?: 'minion' | 'hero';
  room_version: number;
  sequence: number;
  type: string;
  value: number;
};

export type SnapshotCardRef = {
  instance_id: number;
  card_id?: number;
  attack?: number;
  health?: number;
  max_health?: number;
  exhausted?: boolean;
};

export type SnapshotPlayer = {
  user_id: number;
  health: number;
  mana: number;
  max_mana: number;
  deck_count: number;
  hand_count: number;
  board: SnapshotCardRef[];
};

export type RoomSnapshot = {
  state: 'SUCCESS' | 'ERROR';
  message?: string;
  room_id: number;
  match_id: number;
  version?: number;
  last_sequence?: number;
  current_player?: number;
  winner_id?: number;
  room_state?: 'playing' | 'finished' | 'perparing' | 'unknow';
  player_0: SnapshotPlayer;
  player_1: SnapshotPlayer;
  my_hand: SnapshotCardRef[];
};

export type OperationResult = {
  state: 'SUCCESS' | 'FAIL' | 'ERROR';
  version?: number;
  events?: RoomEvent[];
  action_error?: string;
  message?: string;
};

export type RoomStat = {
  state: 'SUCCESS' | 'ERROR';
  room_state?: 'playing' | 'finished' | 'perparing' | 'unknow';
  winner?: number;
  message?: string;
};

export type BattleContext = {
  roomId: number;
  matchId: number;
  opponentId: number;
};

export type CurrentRoom = {
  state: 'SUCCESS' | 'ERROR';
  in_room: boolean;
  room_id?: number;
  match_id?: number;
  opponent_id?: number;
  room_state?: 'playing' | 'finished' | 'perparing' | 'unknow';
  message?: string;
};

export type CardCatalogItem = {
  id: number;
  card_key: string;
  name: string;
  description?: string | null;
  mana_cost: number;
  attack?: number | null;
  health?: number | null;
  card_type: string;
  rarity?: string | null;
  class_type?: string | null;
  effect_json?: string | null;
  image_url?: string | null;
  is_collectible: boolean;
};

export type CardCatalogResponse = {
  state: 'SUCCESS' | 'ERROR';
  cards: CardCatalogItem[];
  count: number;
};

export type OwnedCard = {
  id: number;
  quantity: number;
};

export type OwnedCardsResponse = {
  state: 'SUCCESS' | 'ERROR';
  cards: OwnedCard[];
  count: number;
};

export type DeckSummary = {
  deck_id: number;
  name: string;
  class_type: string;
  is_active: boolean;
};

export type DeckListResponse = {
  state: 'SUCCESS' | 'ERROR';
  decks: DeckSummary[];
};

export type DeckEntry = {
  card_id: number;
  quantity: number;
};

export type DeckDetail = {
  state: 'SUCCESS' | 'ERROR';
  name: string;
  class_type: string;
  is_active: boolean;
  cards: DeckEntry[];
  count_cards: number;
};

export type SaveDeckResponse = {
  state: 'SUCCESS' | 'FAIL' | 'ERROR';
  incomplete?: boolean;
  deck_id?: number;
  message?: string;
};

export type DeckEditorState = {
  mode: 'create' | 'edit';
  deckId?: number;
};
