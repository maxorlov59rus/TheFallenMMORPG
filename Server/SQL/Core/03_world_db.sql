-- ============================================================
-- «Огни душ Падших Валькирий» — игровая БД (ogni_world)
-- Доменные таблицы (расы/классы/души/Падшие) создаёт 01_setting_domains.sql и патчи.
-- Здесь — runtime-таблицы связки auth->world->client.
-- ============================================================
SET NAMES utf8mb4;

CREATE DATABASE IF NOT EXISTS ogni_world CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ogni_world;

-- Игровое состояние персонажа (персонаж-владелец — в ogni_characters.characters, id совпадают)
CREATE TABLE IF NOT EXISTS character_state (
  character_id   BIGINT UNSIGNED NOT NULL PRIMARY KEY,   -- FK смысловой -> ogni_characters.characters.id
  zone_key       VARCHAR(32)  NOT NULL DEFAULT 'vindalr_plains',
  pos_x FLOAT NOT NULL, pos_y FLOAT NOT NULL, pos_z FLOAT NOT NULL,
  yaw            DECIMAL(6,3) NOT NULL DEFAULT 0,
  hp             INT UNSIGNED NOT NULL,
  max_hp         INT UNSIGNED NOT NULL,
  resource_type  ENUM('saga_rage','blood','soul_particles') NOT NULL,
  resource_value INT UNSIGNED NOT NULL DEFAULT 0,
  soul_particles INT UNSIGNED NOT NULL DEFAULT 0,        -- кэш; источник истины — soul_ledger
  level          SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  experience     BIGINT UNSIGNED NOT NULL DEFAULT 0,
  fallen_debuff_until DATETIME NULL,                     -- «пепел утраты» после Зала Павших
  updated_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Активные сессии worldserver'а: handshake C2S_SessionHello резервирует строку
CREATE TABLE IF NOT EXISTS active_session (
  session_id     CHAR(36) NOT NULL PRIMARY KEY,           -- = auth_session.jwt_id
  character_id   BIGINT UNSIGNED NOT NULL,
  world_server_id INT UNSIGNED NOT NULL,
  udp_endpoint   VARCHAR(64) NOT NULL,                    -- ip:port клиента
  seq_in         INT UNSIGNED NOT NULL DEFAULT 0,         -- счётчики reliable layer
  seq_out        INT UNSIGNED NOT NULL DEFAULT 0,
  established_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_char (character_id)
) ENGINE=InnoDB;

-- Реестр world-серверов (шардинг зон)
CREATE TABLE IF NOT EXISTS world_servers (
  world_server_id INT UNSIGNED NOT NULL PRIMARY KEY AUTO_INCREMENT,
  server_key      VARCHAR(32) NOT NULL UNIQUE,
  bind_ip         VARCHAR(45) NOT NULL DEFAULT '0.0.0.0',
  udp_port        SMALLINT UNSIGNED NOT NULL DEFAULT 7777,
  zones_mask      TEXT NOT NULL,                          -- JSON-массив zone_key
  status          ENUM('online','offline','draining') NOT NULL DEFAULT 'offline',
  heartbeat_at    TIMESTAMP NULL
) ENGINE=InnoDB;

INSERT INTO world_servers (server_key, zones_mask) VALUES
 ('ws-vindalr', '["vindalr_plains","asgard_fields"]')
ON DUPLICATE KEY UPDATE zones_mask = VALUES(zones_mask);

-- Телеметрия TCP 7778 (античит: swing-rate, speed-hack кандидаты)
CREATE TABLE IF NOT EXISTS telemetry_events (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  session_id CHAR(36) NOT NULL,
  kind ENUM('swing','move','cast','collect','forge','rep') NOT NULL,
  stamp_ms BIGINT NOT NULL,                              -- клиентский timestamp для lag-rewind
  payload JSON NOT NULL,
  at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_sess_time (session_id, at)
) ENGINE=InnoDB;

CREATE USER IF NOT EXISTS 'ogni'@'localhost' IDENTIFIED BY 'ogni_dev_pw';
GRANT SELECT, INSERT, UPDATE, DELETE, EXECUTE ON ogni_world.* TO 'ogni'@'localhost';
FLUSH PRIVILEGES;
