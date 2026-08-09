import type {
  CardCatalogResponse,
  CurrentRoom,
  DeckDetail,
  DeckListResponse,
  MatchResponse,
  OperationResult,
  OwnedCardsResponse,
  Profile,
  RoomEvent,
  RoomSnapshot,
  RoomStat,
  SaveDeckResponse,
} from './types';

async function readBody(response: Response) {
  const contentType = response.headers.get('content-type') ?? '';
  if (contentType.includes('application/json')) {
    return response.json();
  }
  return response.text();
}

async function request<T>(input: RequestInfo | URL, init?: RequestInit): Promise<T> {
  const response = await fetch(input, {
    credentials: 'include',
    ...init,
    headers: {
      ...(init?.body instanceof FormData ? {} : { 'Content-Type': 'application/json' }),
      ...init?.headers,
    },
  });
  const body = await readBody(response);
  if (!response.ok) {
    const message =
      typeof body === 'string' ? body : body.message ?? body.error ?? 'request failed';
    throw new Error(message);
  }
  return body as T;
}

function formBody(values: Record<string, string>) {
  return new URLSearchParams(values).toString();
}

export async function login(username: string, password: string) {
  return request<string>('/user/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: formBody({ username, password }),
  });
}

export async function register(username: string, password: string) {
  return request<string>('/user/register', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: formBody({ username, password }),
  });
}

export async function logout() {
  return request<string>('/user/logout', { method: 'POST' });
}

export async function getStatus() {
  return request<string>('/user/status');
}

export async function getProfile() {
  return request<Profile>('/profile/stat');
}

export async function getCardCatalog() {
  return request<CardCatalogResponse>('/cards/catalog');
}

export async function getMyCards() {
  return request<OwnedCardsResponse>('/cards/my');
}

export async function getDecks() {
  return request<DeckListResponse>('/decks/');
}

export async function getDeck(deckId: number) {
  return request<DeckDetail>(`/decks/${deckId}`);
}

export async function createDeck(name: string, cards: { card_id: number; quantity: number }[]) {
  return request<SaveDeckResponse>('/decks/', {
    method: 'POST',
    body: JSON.stringify({ name, cards }),
  });
}

export async function updateDeck(
  deckId: number,
  name: string,
  cards: { card_id: number; quantity: number }[],
) {
  return request<SaveDeckResponse>(`/decks/${deckId}`, {
    method: 'POST',
    body: JSON.stringify({ name, cards }),
  });
}

export async function joinMatch(deckId: number) {
  return request<MatchResponse>(`/matchfind/join?deckid=${deckId}`, { method: 'POST' });
}

export async function pollMatch() {
  return request<MatchResponse>('/matchfind/poll');
}

export async function cancelMatch() {
  return request<MatchResponse>('/matchfind/cancel', { method: 'POST' });
}

export async function getSnapshot(roomId: number) {
  return request<RoomSnapshot>(`/battleroom/${roomId}/snapshot`);
}

export async function getCurrentRoom() {
  return request<CurrentRoom>('/battleroom/current');
}

export async function pollRoom(roomId: number, sequence: number) {
  return request<{ state: 'SUCCESS' | 'ERROR'; events?: RoomEvent[]; message?: string }>(
    `/battleroom/${roomId}/poll`,
    {
      method: 'POST',
      body: JSON.stringify({ sequence }),
    },
  );
}

export async function sendOperation(
  roomId: number,
  type: 'play card' | 'end turn' | 'surrender',
  version: number,
  requestId: number,
  cardInstance = 0,
) {
  return request<OperationResult>(`/battleroom/${roomId}/operation`, {
    method: 'POST',
    body: JSON.stringify({
      type,
      version,
      request_id: requestId,
      card_instance: cardInstance,
      target: -1,
    }),
  });
}

export async function getRoomStat(roomId: number) {
  return request<RoomStat>(`/battleroom/${roomId}/stat`);
}

export async function leaveRoom(roomId: number) {
  return request<{ state: string }>(`/battleroom/${roomId}/leave`, { method: 'POST' });
}
