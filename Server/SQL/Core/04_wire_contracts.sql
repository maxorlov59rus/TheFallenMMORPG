-- ============================================================
-- 04_wire_contracts.sql — контракт пакетного обмена (Docs/PROTOCOL.md) в табличном виде.
-- Мир-БД: реестр сообщений + валидация payload по JSON-схеме.
-- ============================================================
USE ogni_world;

CREATE TABLE IF NOT EXISTS msg_registry (
  msg_id     SMALLINT UNSIGNED NOT NULL PRIMARY KEY,   -- 0x1001 и т.д.
  direction  ENUM('C2S','S2C') NOT NULL,
  msg_name   VARCHAR(48) NOT NULL UNIQUE,
  reliable   TINYINT(1) NOT NULL DEFAULT 1,            -- требует ack/retransmit
  rate_limit_per_sec SMALLINT UNSIGNED NULL,           -- C2S антифлуд (MaxPacketsPerSecond поверх)
  schema_json JSON NOT NULL,                           -- контракт полей для codegen клиента/сервера
  since_ver  TINYINT UNSIGNED NOT NULL DEFAULT 1
) ENGINE=InnoDB;

INSERT INTO msg_registry (msg_id, direction, msg_name, reliable, rate_limit_per_sec, schema_json) VALUES
 (0x1001,'C2S','MoveUpdate',      0, 30, '{"pos":"f32x3","vel":"f32x3","yaw":"f32","stamp_ms":"u64"}'),
 (0x1002,'C2S','SwingBegin',      1, 12, '{"weapon_id":"u32","dir":"f32x3","cone_deg":"f32","reach_m":"f32","stamp_ms":"u64"}'),
 (0x1003,'C2S','CastAbility',     1, 10, '{"ability_id":"u32","target_guid":"u64","aim_pos":"f32x3"}'),
 (0x1004,'C2S','ForgeCraft',      1,  2, '{"recipe_id":"u32"}'),
 (0x1005,'C2S','CollectSoulDrop', 1, 20, '{"drop_guid":"u64"}'),
 (0x1006,'C2S','ClanPing',        1,  1, '{"clan_key":"str32"}'),
 (0x2001,'S2C','SpawnEntity',     1, NULL, '{"guid":"u64","kind":"u8","template_entry":"u32","pos":"f32x3","state":"u8"}'),
 (0x2002,'S2C','DespawnEntity',   1, NULL, '{"guid":"u64"}'),
 (0x2010,'S2C','SwingResult',     1, NULL, '{"attacker":"u64","victim":"u64","outcome":"hit|miss|glanced","dmg":"u32","soul_gain":"u16"}'),
 (0x2011,'S2C','AbilityAck',      1, NULL, '{"ability_id":"u32","ack":"bool","reason":"str32"}'),
 (0x2020,'S2C','WorldState',      1, NULL, '{"zone_key":"str32","kv":"map<str,str>"}'),
 (0x2021,'S2C','DeathLastChance', 1, NULL, '{"ttl_sec":"u8"}'),
 (0x2030,'S2C','SoulDelta',       1, NULL, '{"delta":"i32","reason":"str32"}'),
 (0x2031,'S2C','ForgeMenu',       1, NULL, '{"recipes":"arr<u32>"}'),
 (0x2040,'S2C','RepChange',       1, NULL, '{"clan_key":"str32","delta":"i16","new_score":"i16"}'),
 (0x2F00,'S2C','SessionAccept',   1, NULL, '{"session_key":"b64","aoi_seed":"u32"}'),
 (0x2F01,'S2C','ProtocolMismatch',1, NULL, '{"server_ver":"u8","min_build":"str32"}')
ON DUPLICATE KEY UPDATE schema_json = VALUES(schema_json);

-- Тестовые данные стенда (dev-only; prod чистится install-скриптом)
USE ogni_auth;
INSERT INTO account (username, email, pass_hash, status) VALUES
 ('testvala', 'vala@ogni.dev', '$argon2id$v=19$m=65536,t=3,p=4$c2ZjYWZlZQ$PLACEHOLDER_DEV_HASH', 'active')
ON DUPLICATE KEY UPDATE status='active';

INSERT INTO characters (account_id, name, race_key, class_key, level)
SELECT id, 'Sigrid', 'einherjar', 'berserker', 1 FROM account WHERE username='testvala'
ON DUPLICATE KEY UPDATE level = VALUES(level);

INSERT IGNORE INTO ogni_world.character_state (character_id, zone_key, pos_x, pos_y, pos_z, hp, max_hp, resource_type)
SELECT c.id, 'vindalr_plains', 10.0, 20.0, 0.0, 120, 120, 'saga_rage'
FROM characters c JOIN account a ON a.id=c.account_id WHERE a.username='testvala';
