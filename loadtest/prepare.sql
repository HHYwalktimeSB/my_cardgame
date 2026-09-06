\if :{?matches}
\else
\set matches 1
\endif

\if :{?run_id}
\else
\set run_id local
\endif

BEGIN;

WITH repeated_effects AS (
    SELECT jsonb_build_object(
        'battlecry', (
            SELECT jsonb_agg(jsonb_build_object(
                'type', 'heal',
                'target', 'friendly_hero',
                'value', 1))
            FROM generate_series(1, 16)
        ),
        'deathrattle', (
            SELECT jsonb_agg(jsonb_build_object(
                'type', 'heal',
                'target', 'friendly_hero',
                'value', 1))
            FROM generate_series(1, 32)
        )
    ) AS effect_json
)
INSERT INTO cards (
    card_key,
    name,
    description,
    mana_cost,
    attack,
    health,
    card_type,
    rarity,
    class_type,
    effect_json,
    image_url,
    is_collectible)
SELECT
    format('stress_chain_%s', lpad(card_number::text, 2, '0')),
    format('Stress Chain %s', card_number),
    'Load-test card: 16 battlecry events and 32 deathrattle events.',
    0,
    1,
    1,
    'minion',
    'test',
    'unused',
    repeated_effects.effect_json,
    NULL,
    TRUE
FROM generate_series(1, 15) AS card_number
CROSS JOIN repeated_effects
ON CONFLICT (card_key) DO UPDATE SET
    mana_cost = EXCLUDED.mana_cost,
    attack = EXCLUDED.attack,
    health = EXCLUDED.health,
    effect_json = EXCLUDED.effect_json,
    image_url = NULL;

INSERT INTO users (username, password_hash)
SELECT
    format('stress_%s_%s', :'run_id', player_number),
    '548d8323ad32cbd4c898b0cb6c48947b828de553ea51f6aa4bd0e5fea6ebf5a5'
FROM generate_series(1, :matches * 2) AS player_number
ON CONFLICT (username) DO UPDATE SET
    password_hash = EXCLUDED.password_hash;

INSERT INTO player_cards (user_id, card_id, quantity)
SELECT users.id, cards.id, 2
FROM generate_series(1, :matches * 2) AS player_number
JOIN users
  ON users.username = format('stress_%s_%s', :'run_id', player_number)
CROSS JOIN generate_series(1, 15) AS card_number
JOIN cards
  ON cards.card_key = format(
      'stress_chain_%s', lpad(card_number::text, 2, '0'))
ON CONFLICT (user_id, card_id) DO UPDATE SET
    quantity = EXCLUDED.quantity;

INSERT INTO decks (user_id, name, class_type, is_active)
SELECT
    users.id,
    format('stress-load-%s', :'run_id'),
    'unused',
    TRUE
FROM users
WHERE users.username IN (
      SELECT format('stress_%s_%s', :'run_id', player_number)
      FROM generate_series(1, :matches * 2) AS player_number
  )
  AND NOT EXISTS (
      SELECT 1
      FROM decks
      WHERE decks.user_id = users.id
        AND decks.name = format('stress-load-%s', :'run_id')
  );

INSERT INTO deck_cards (deck_id, card_id, quantity)
SELECT decks.id, cards.id, 2
FROM generate_series(1, :matches * 2) AS player_number
JOIN users
  ON users.username = format('stress_%s_%s', :'run_id', player_number)
JOIN decks
  ON decks.user_id = users.id
 AND decks.name = format('stress-load-%s', :'run_id')
CROSS JOIN generate_series(1, 15) AS card_number
JOIN cards
  ON cards.card_key = format(
      'stress_chain_%s', lpad(card_number::text, 2, '0'))
ON CONFLICT (deck_id, card_id) DO UPDATE SET
    quantity = EXCLUDED.quantity;

COMMIT;

SELECT
    :matches AS prepared_matches,
    :matches * 2 AS prepared_players,
    (SELECT count(*)
     FROM cards
     WHERE card_key IN (
         SELECT format(
             'stress_chain_%s', lpad(card_number::text, 2, '0'))
         FROM generate_series(1, 15) AS card_number
     )) AS stress_cards;
