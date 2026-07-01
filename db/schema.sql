CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(32) UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

CREATE TABLE cards (
    id BIGSERIAL PRIMARY KEY,
    card_key VARCHAR(64) UNIQUE NOT NULL,
    name VARCHAR(64) NOT NULL,
    description TEXT,
    mana_cost INT NOT NULL DEFAULT 0,
    attack INT,
    health INT,
    card_type VARCHAR(32) NOT NULL,
    rarity VARCHAR(32),
    class_type VARCHAR(32),
    effect_json JSONB,
    image_url TEXT,
    is_collectible BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

CREATE TABLE player_cards (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    card_id BIGINT NOT NULL REFERENCES cards(id),
    quantity INT NOT NULL DEFAULT 0,
    UNIQUE (user_id, card_id)
);

CREATE TABLE decks (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(64) NOT NULL,
    class_type VARCHAR(32),
    is_active BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

CREATE TABLE deck_cards (
    id BIGSERIAL PRIMARY KEY,
    deck_id BIGINT NOT NULL REFERENCES decks(id) ON DELETE CASCADE,
    card_id BIGINT NOT NULL REFERENCES cards(id),
    quantity INT NOT NULL,
    UNIQUE (deck_id, card_id)
);

CREATE TABLE matches (
    id BIGSERIAL PRIMARY KEY,
    match_key VARCHAR(64) UNIQUE NOT NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'FINISHED',
    game_mode VARCHAR(32) NOT NULL DEFAULT 'CASUAL',
    winner_id BIGINT REFERENCES users(id),
    started_at TIMESTAMP,
    ended_at TIMESTAMP,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

CREATE TABLE match_players (
    id BIGSERIAL PRIMARY KEY,
    match_id BIGINT NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id),
    deck_id BIGINT REFERENCES decks(id),
    result VARCHAR(32) NOT NULL,
    rating_before INT,
    rating_after INT,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    UNIQUE (match_id, user_id)
);

CREATE TABLE match_events (
    id BIGSERIAL PRIMARY KEY,
    match_id BIGINT NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
    turn_number INT NOT NULL DEFAULT 0,
    sequence_number INT NOT NULL,
    user_id BIGINT REFERENCES users(id),
    event_type VARCHAR(64) NOT NULL,
    event_payload JSONB NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    UNIQUE (match_id, sequence_number)
);

CREATE TABLE player_stats (
    user_id BIGINT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    rating INT NOT NULL DEFAULT 1000,
    wins INT NOT NULL DEFAULT 0,
    losses INT NOT NULL DEFAULT 0,
    draws INT NOT NULL DEFAULT 0,
    total_matches INT NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT NOW()
);
