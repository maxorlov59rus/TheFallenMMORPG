-- ============================================================
-- «Огни душ Падших Валькирий» — БД авторизации (ogni_auth)
-- Target: MySQL 8.0 / MariaDB 10.6+ / InnoDB / utf8mb4
-- Привязка к протоколу: Docs/PROTOCOL.md (JWT -> C2S_SessionHello)
-- ============================================================
SET NAMES utf8mb4;

CREATE DATABASE IF NOT EXISTS ogni_auth CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ogni_auth;

-- Учётные записи. Хэш: Argon2id (код в скобках параметрами строки хэша).
CREATE TABLE IF NOT EXISTS account (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username      VARCHAR(32)  NOT NULL UNIQUE,           -- регистронезависимо ниже по collation
  email         VARCHAR(190) NULL UNIQUE,
  pass_hash     VARCHAR(255) NOT NULL,                  -- $argon2id$v=19$m=65536,t=3,p=4$...
  status        ENUM('active','banned','unactivated') NOT NULL DEFAULT 'unactivated',
  gm_level      TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 0 игрок .. 4 root
  failed_logins SMALLINT UNSIGNED NOT NULL DEFAULT 0,   -- anti-bruteforce счётчик
  locked_until  DATETIME NULL,                          -- до этого времени логин запрещён
  created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login_at TIMESTAMP NULL
) ENGINE=InnoDB;

-- Сессии JWT: сервер игрового мира проверяет подпись у authserver либо по этой таблице (revocation list).
CREATE TABLE IF NOT EXISTS auth_session (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id   BIGINT UNSIGNED NOT NULL,
  jwt_id       CHAR(36)     NOT NULL UNIQUE,            -- 'jti' claim
  session_key  VARBINARY(64) NOT NULL,                  -- x25519-aes-gcm ключ UDP-канала (засекреченный AES-blob)
  ip           VARBINARY(16) NOT NULL,
  client_build VARCHAR(32)  NOT NULL,                    -- версия патчера клиента UE6
  issued_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at   TIMESTAMP NOT NULL,                       -- TTL токена (authserver.conf TokenTtlSec)
  revoked_at   TIMESTAMP NULL,
  world_server_id INT UNSIGNED NULL,                    -- к какому worldserver прибиндена
  FOREIGN KEY (account_id) REFERENCES account(id) ON DELETE CASCADE,
  INDEX idx_expires (expires_at),
  INDEX idx_account (account_id)
) ENGINE=InnoDB;

-- Персонажи принадлежат аккаунту; world-БД хранит только игровые состояния.
CREATE TABLE IF NOT EXISTS characters (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id  BIGINT UNSIGNED NOT NULL,
  name        VARCHAR(24) NOT NULL UNIQUE,
  race_key    VARCHAR(32) NOT NULL,                     -- FK смысловой -> ogni_world.racial_def
  class_key   VARCHAR(32) NOT NULL,                     -- -> ogni_world.class_def
  clan_id     INT UNSIGNED NULL,                        -- -> ogni_world.clan_def (вступление через геймплей)
  level       SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  deleted_at  TIMESTAMP NULL,                           -- soft-delete, восстановление 30 дней
  FOREIGN KEY (account_id) REFERENCES account(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Аудит входов (античит/поддержка)
CREATE TABLE IF NOT EXISTS login_audit (
  id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id BIGINT UNSIGNED NULL,
  ip         VARBINARY(16) NOT NULL,
  result     ENUM('ok','bad_password','unknown_user','locked','revoked_token','build_mismatch') NOT NULL,
  at         TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_time (at)
) ENGINE=InnoDB;

-- Белый список билдов клиента (патчер): world/auth отклоняют старые версии
CREATE TABLE IF NOT EXISTS allowed_builds (
  build_tag  VARCHAR(32) PRIMARY KEY,
  min_client_version DECIMAL(6,2) NOT NULL,
  notes      VARCHAR(255) NULL,
  active     TINYINT(1) NOT NULL DEFAULT 1
) ENGINE=InnoDB;

INSERT INTO allowed_builds (build_tag, min_client_version, notes) VALUES
 ('ogni-p0-dev', 0.1, 'dev-билд стенда')
ON DUPLICATE KEY UPDATE notes = VALUES(notes);

-- Сервисный пользователь (пароль менять при развёртывании!)
CREATE USER IF NOT EXISTS 'ogni'@'localhost' IDENTIFIED BY 'ogni_dev_pw';
GRANT SELECT, INSERT, UPDATE, DELETE, EXECUTE ON ogni_auth.* TO 'ogni'@'localhost';
FLUSH PRIVILEGES;
