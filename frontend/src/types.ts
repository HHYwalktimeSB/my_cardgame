export type View = 'login' | 'profile' | 'queue' | 'room' | 'result';

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
  room_version: number;
  sequence: number;
  type: string;
  value: number;
};

export type SnapshotPlayer = {
  user_id: number;
  health: number;
  mana: number;
  max_mana: number;
  deck_count: number;
  hand_count: number;
  board: number[];
};

export type RoomSnapshot = {
  status: 'SUCCESS' | 'ERROR';
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
  my_hand: number[];
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
