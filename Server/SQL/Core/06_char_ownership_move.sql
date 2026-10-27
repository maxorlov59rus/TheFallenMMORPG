-- ============================================================
-- 06_char_ownership_move.sql — Перенос владения персонажами из ogni_auth в ogni_characters.
-- Зоны ответственности:
--   ogni_auth        -> account, auth_session, login_attempt/audit (аутентификация)
--   ogni_characters  -> characters (владелец account_id), char_template, character_appearance,
--                       character_snapshot, name_policy (регистрация + редактор)
--   ogni_world       -> character_state, active_session (runtime)
-- Тонкий канал auth->world: validate-response содержит {sub, username, flags}; миру недоступна БД auth.
-- Идемпотентен. Порядок применения: после 05.
-- ============================================================
SET NAMES utf8mb4;

CREATE DATABASE IF NOT EXISTS ogni_characters CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ogni_characters;

CREATE TABLE IF NOT EXISTS characters (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id BIGINT UNSIGNED NOT NULL,
  name VARCHAR(24) NOT NULL,
  race_key VARCHAR(32) NOT NULL,
  class_key VARCHAR(32) NOT NULL,
  clan_id INT UNSIGNED DEFAULT NULL,
  level SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  deleted_at TIMESTAMP NULL DEFAULT NULL,
  UNIQUE KEY name (name),
  KEY idx_account (account_id)
) ENGINE=InnoDB;

-- Перенос данных выполняется ОДИН РАЗ вручную при миграции стенда (см. комментарий в конце файла);
-- для чистой установки таблицы уже пусты и перенос не требуется.

-- FK appearance/snapshot теперь внутри ogni_characters.
SET @fk := (SELECT CONSTRAINT_NAME FROM information_schema.KEY_COLUMN_USAGE
            WHERE TABLE_SCHEMA='ogni_characters' AND TABLE_NAME='character_appearance'
              AND COLUMN_NAME='character_id' AND REFERENCED_TABLE_NAME IS NOT NULL LIMIT 1);
SET @s := IF(@fk IS NULL, 'SELECT 1', CONCAT('ALTER TABLE character_appearance DROP FOREIGN KEY ',@fk));
PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st;
ALTER TABLE character_appearance
  ADD CONSTRAINT fk_app_char FOREIGN KEY (character_id) REFERENCES characters(id) ON DELETE CASCADE;

-- В ogni_auth таблица characters переименовывается в архив (не удаляется до подтверждения прода).
-- Ручной шаг на стенде: RENAME TABLE ogni_auth.characters TO ogni_auth.characters_deprecated_<date>;
