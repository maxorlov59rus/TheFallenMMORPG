-- ============================================================
-- Патч 3/3: valkyrie_ressurrection_mechanic.sql
-- «Последний шанс»: таймер смерти, Зов валькирии, Зал Павших
-- Зависимости: SQL/Core/01_setting_domains.sql, патчи 1–2
-- ============================================================

-- ---- Конфиг механики (единая строка-параметры для worldserver) ----
CREATE TABLE IF NOT EXISTS death_mechanic_config (
  cfg_key VARCHAR(48) NOT NULL PRIMARY KEY,
  cfg_value VARCHAR(64) NOT NULL,
  comment VARCHAR(191) NULL
) ENGINE=InnoDB;

INSERT INTO death_mechanic_config (cfg_key, cfg_value, comment) VALUES
 ('death_timer_seconds',      '8',    'Окно «последнего шанса» после HP==0'),
 ('valkyrie_revive_hp_pct',   '20',   'Здоровье при воскрешении на месте'),
 ('death_soul_loss_pct',      '5',    'Потеря частиц душ при timeout-смерти (долг Гиннунгагапу)'),
 ('battle_sorrow_debuff_sec', '60',   'Дебафф «Тоска по бою» после возвращения из Зала Павших'),
 ('valkyrie_call_cd_linked',  '1',    'Кулдаун Зова привязан к запасу частиц кастера')
ON DUPLICATE KEY UPDATE cfg_value=VALUES(cfg_value);

-- ---- Состояние павших игроков (рантайм-таблица, чистится каждые сутки) ----
CREATE TABLE IF NOT EXISTS player_death_state (
  player_guid BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  zone_key VARCHAR(32) NOT NULL,
  pos_x FLOAT NOT NULL, pos_y FLOAT NOT NULL, pos_z FLOAT NOT NULL,
  died_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  state ENUM('last_chance','sent_to_hall','revived') NOT NULL DEFAULT 'last_chance',
  INDEX idx_state_time (state, died_at)
) ENGINE=InnoDB;

-- ---- Дебафф «Тоска по бою»: generic aura-таблица (замена WOW-аур) ----
CREATE TABLE IF NOT EXISTS aura_state (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  target_guid BIGINT UNSIGNED NOT NULL,
  ability_id INT UNSIGNED NOT NULL,
  stacks TINYINT UNSIGNED NOT NULL DEFAULT 1,
  applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at TIMESTAMP NOT NULL,
  UNIQUE KEY uq_target_ability (target_guid, ability_id),
  FOREIGN KEY (ability_id) REFERENCES ability_def(id)
) ENGINE=InnoDB;

INSERT INTO ability_def (id, ability_key, display_name, school, resource_cost_type, flags) VALUES
 (90201,'debuff_battle_sorrow','Тоска по бою (-15% урона)','soul','none','')
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name);

-- ---- Хранимая процедура: смерть игрока (вызывается движком Unit::Die override) ----
-- Позиция игрока в мире (рантайм-таблица; полная — в core). Создаётся до процедур.
CREATE TABLE IF NOT EXISTS player_world_pos (
  player_guid BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  zone_key VARCHAR(32) NOT NULL,
  pos_x FLOAT NOT NULL DEFAULT 0, pos_y FLOAT NOT NULL DEFAULT 0, pos_z FLOAT NOT NULL DEFAULT 0
) ENGINE=InnoDB;

DELIMITER //
CREATE PROCEDURE sp_player_died(IN p_guid BIGINT UNSIGNED, IN p_zone VARCHAR(32))
proc_label: BEGIN
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; END;

  -- уже мёртв? идемпотентность
  IF EXISTS (SELECT 1 FROM player_death_state WHERE player_guid=p_guid AND state='last_chance'
             AND died_at > NOW() - INTERVAL 30 SECOND) THEN
    LEAVE proc_label;
  END IF;

  INSERT INTO player_death_state (player_guid, zone_key, pos_x, pos_y, pos_z)
    SELECT p_guid, p_zone, pos_x, pos_y, pos_z FROM player_world_pos WHERE player_guid=p_guid
  ON DUPLICATE KEY UPDATE died_at=NOW(), state='last_chance', zone_key=p_zone;
END//
DELIMITER ;

-- ---- Хранимая процедура: Зов валькирии (проверка ресурсов + revive) ----
DELIMITER //
CREATE PROCEDURE sp_valkyrie_call(IN p_caster BIGINT UNSIGNED, IN p_target BIGINT UNSIGNED,
                                  OUT p_ok TINYINT, OUT p_msg VARCHAR(128))
proc_label: BEGIN
  DECLARE v_cost INT; DECLARE v_have INT; DECLARE v_hp_pct INT; DECLARE v_sec_left INT;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; SET p_ok=0; SET p_msg='internal_error'; END;

  SELECT resource_cost INTO v_cost FROM ability_def WHERE id=90001;

  -- цель должна быть в окне last_chance
  SELECT TIMESTAMPDIFF(SECOND, died_at, NOW()) INTO v_sec_left
    FROM player_death_state WHERE player_guid=p_target AND state='last_chance';
  IF v_sec_left IS NULL THEN SET p_ok=0; SET p_msg='not_last_chance'; LEAVE proc_label; END IF;
  SELECT CAST(cfg_value AS SIGNED) INTO v_hp_pct FROM death_mechanic_config WHERE cfg_key='death_timer_seconds';
  IF v_sec_left > v_hp_pct THEN
     UPDATE player_death_state SET state='sent_to_hall' WHERE player_guid=p_target;
     SET p_ok=0; SET p_msg='window_expired'; LEAVE proc_label;
  END IF;

  -- ресурс кастера: частицы душ
  SELECT soul_particles INTO v_have FROM player_soul_state WHERE player_guid=p_caster;
  IF v_have IS NULL OR v_have < v_cost THEN SET p_ok=0; SET p_msg='insufficient_souls'; LEAVE proc_label; END IF;

  START TRANSACTION;
    UPDATE player_soul_state SET soul_particles = soul_particles - v_cost WHERE player_guid=p_caster;
    INSERT INTO soul_ledger (player_guid, delta, reason, ref_id)
      VALUES (p_caster, -CAST(v_cost AS SIGNED), 'ability', 'valkyrie_call');
    UPDATE player_death_state SET state='revived' WHERE player_guid=p_target;
    -- revive on spot, HP=20% — фактическое лечение делает движок по событию ReviveOnSpot
    SET p_ok=1; SET p_msg='revived_on_spot';
  COMMIT;
END//
DELIMITER ;

-- ---- Хранимая процедура: timeout -> Зал Павших (телепорт + soul tax + дебафф) ----
DELIMITER //
CREATE PROCEDURE sp_send_to_hall(IN p_guid BIGINT UNSIGNED, OUT p_ok TINYINT)
BEGIN
  DECLARE v_have INT; DECLARE v_tax INT; DECLARE v_loss_pct INT;
  DECLARE hx FLOAT; DECLARE hy FLOAT; DECLARE hz FLOAT;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; SET p_ok=0; END;

  SELECT CAST(cfg_value AS SIGNED) INTO v_loss_pct FROM death_mechanic_config WHERE cfg_key='death_soul_loss_pct';

  START TRANSACTION;
    SELECT soul_particles INTO v_have FROM player_soul_state WHERE player_guid=p_guid FOR UPDATE;
    IF v_have IS NOT NULL THEN
      SET v_tax = FLOOR(v_have * v_loss_pct / 100);
      UPDATE player_soul_state SET soul_particles = soul_particles - v_tax WHERE player_guid=p_guid;
      INSERT INTO soul_ledger (player_guid, delta, reason, ref_id) VALUES (p_guid, -v_tax, 'death_tax', 'hall_of_fallen');
    END IF;

    UPDATE player_death_state SET state='sent_to_hall' WHERE player_guid=p_guid;

    -- точка телепорта: ближайший/главный Зал Павших
    SELECT pos_x, pos_y, pos_z INTO hx, hy, hz FROM hall_of_fallen_loc ORDER BY loc_id LIMIT 1;
    UPDATE player_world_pos SET zone_key='hall_primordial', pos_x=hx, pos_y=hy, pos_z=hz WHERE player_guid=p_guid;

    -- дебафф «Тоска по бою»
    INSERT INTO aura_state (target_guid, ability_id, expires_at)
      VALUES (p_guid, 90201, NOW() + INTERVAL 60 SECOND)
    ON DUPLICATE KEY UPDATE applied_at=NOW(), expires_at=NOW() + INTERVAL 60 SECOND, stacks=1;
  COMMIT;
  SET p_ok=1;
END//
DELIMITER ;

-- ---- Watchdog: движок раз в тик вызывает выборку истёкших last_chance ----
-- (в C++: SELECT player_guid FROM player_death_state WHERE state='last_chance'
--   AND died_at < NOW() - INTERVAL (death_timer_seconds) SECOND -> sp_send_to_hall)
