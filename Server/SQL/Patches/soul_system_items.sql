-- ============================================================
-- Патч 2/3: soul_system_items.sql
-- Предметы, впитывающие души; дроп-таблицы частиц; Огни душ как объекты мира
-- Зависимости: SQL/Core/01_setting_domains.sql, патч 1
-- ============================================================

-- ---- Таблица предметов (аналог item_template, без Blizzard-формата) ----
CREATE TABLE IF NOT EXISTS item_def (
  item_key VARCHAR(64) NOT NULL PRIMARY KEY,
  display_name VARCHAR(96) NOT NULL,
  kind ENUM('weapon','armor','amulet','ingot','core','consumable') NOT NULL,
  rarity ENUM('common','rare','mythic','fallen') NOT NULL DEFAULT 'common',
  soul_absorb_pct DECIMAL(5,2) NOT NULL DEFAULT 0.00,   -- % авто-сбора частиц при убийстве
  soul_capacity_bonus INT UNSIGNED NOT NULL DEFAULT 0,
  req_class VARCHAR(32) NULL,
  stats_json JSON NULL                                   -- {might:.., insight:.., spirit:..}
) ENGINE=InnoDB;

INSERT INTO item_def (item_key, display_name, kind, rarity, soul_absorb_pct, soul_capacity_bonus, req_class, stats_json) VALUES
 ('weapon_soul_eater','Клык Пожирателя Душ','weapon','fallen', 15.00, 0, 'seidhkona', '{"spirit":18,"soul_on_hit":3}'),
 ('rune_blade_frost','Руна-клинок Ингея','weapon','mythic', 5.00, 0, 'berserker', '{"might":22,"crit_rune":8}'),
 ('amulet_ginnungagap','Амулет Гиннунгагапа','amulet','mythic', 0.00, 50, NULL, '{"hp_flat":220}'),
 ('ingot_wrought_iron','Слиток кованого железа','ingot','common',0.00,0,NULL,NULL),
 ('core_frostshard','Ядро морозного чертога','core','rare',0.00,0,NULL,NULL),
 ('ember_spark','Искра-уголёк (расходник)','consumable','common',0.00,0,NULL,'{"soul_restore":20}');

-- ---- ДРОП ЧАСТИЦ: формула и таблицы ----
CREATE TABLE IF NOT EXISTS soul_drop_rule (
  rule_id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  creature_type ENUM('beast','draugr','jotun','alfar','valkyrie','fenrirkin') NOT NULL,
  level_min TINYINT UNSIGNED NOT NULL,
  level_max TINYINT UNSIGNED NOT NULL,
  base_particles SMALLINT UNSIGNED NOT NULL,
  UNIQUE KEY uq_rule (creature_type, level_min, level_max)
) ENGINE=InnoDB;

INSERT INTO soul_drop_rule (creature_type, level_min, level_max, base_particles) VALUES
 ('beast',1,20,4), ('beast',21,40,7),
 ('draugr',1,20,6), ('draugr',21,40,10),
 ('jotun',1,40,14),
 ('valkyrie',1,40,22),
 ('fenrirkin',1,40,9);

-- Журнал дропов для античита (кто, где, сколько собрал)
CREATE TABLE IF NOT EXISTS soul_drop_log (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  player_guid BIGINT UNSIGNED NOT NULL,
  creature_entry VARCHAR(64) NOT NULL,
  zone_key VARCHAR(32) NOT NULL,
  particles INT NOT NULL,
  auto_collected TINYINT(1) NOT NULL DEFAULT 0,
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_player (player_guid, created_at)
) ENGINE=InnoDB;

-- ---- ОГНИ ДУШ как интерактивные объекты мира ----
CREATE TABLE IF NOT EXISTS soul_forge_node (
  node_id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  zone_key VARCHAR(32) NOT NULL,
  pos_x FLOAT NOT NULL, pos_y FLOAT NOT NULL, pos_z FLOAT NOT NULL,
  is_respawn_point TINYINT(1) NOT NULL DEFAULT 1,
  clan_owner INT UNSIGNED NULL,          -- контроль клана через репутацию
  active TINYINT(1) NOT NULL DEFAULT 1,
  FOREIGN KEY (clan_owner) REFERENCES clan_def(clan_id)
) ENGINE=InnoDB;

INSERT INTO soul_forge_node (zone_key, pos_x, pos_y, pos_z, clan_owner) VALUES
 ('asgard_fields', 120.5, -44.0, 8.2, NULL),
 ('forge_halls',   10.0,  22.5, 0.0, (SELECT clan_id FROM clan_def WHERE clan_key='iron_house')),
 ('frost_steppe', -88.0, 140.0, 31.5,(SELECT clan_id FROM clan_def WHERE clan_key='winterwolf_tribe'));

-- Хранимая процедура: крафт на Огне (вызывается Lua/C++ после проверки наличия)
-- Инвентарь создаётся раньше процедуры (порядок зависимостей).
CREATE TABLE IF NOT EXISTS player_inventory (
  player_guid BIGINT UNSIGNED NOT NULL,
  item_key VARCHAR(64) NOT NULL,
  qty INT UNSIGNED NOT NULL DEFAULT 1,
  slot VARCHAR(16) NULL,
  PRIMARY KEY (player_guid, item_key),
  FOREIGN KEY (item_key) REFERENCES item_def(item_key)
) ENGINE=InnoDB;

DELIMITER //
CREATE PROCEDURE sp_soul_forge_craft(
  IN p_player BIGINT UNSIGNED,
  IN p_recipe INT UNSIGNED,
  OUT p_ok TINYINT, OUT p_msg VARCHAR(128))
proc_label: BEGIN
  DECLARE v_cost INT; DECLARE v_have INT; DECLARE v_mat VARCHAR(64); DECLARE v_out VARCHAR(64);
  DECLARE v_dark TINYINT; DECLARE v_shatter DECIMAL(5,2); DECLARE v_roll DECIMAL(5,2);
  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; SET p_ok=0; SET p_msg='internal_error'; END;

  SELECT soul_cost, material_item_key, shatter_chance_pct, output_item_key
    INTO v_cost, v_mat, v_shatter, v_out
    FROM soul_forge_recipe WHERE recipe_id=p_recipe;
  IF v_cost IS NULL THEN SET p_ok=0; SET p_msg='no_recipe'; LEAVE proc_label; END IF;

  SELECT soul_particles INTO v_have FROM player_soul_state WHERE player_guid=p_player;
  IF v_have IS NULL OR v_have < v_cost THEN SET p_ok=0; SET p_msg='insufficient_souls'; LEAVE proc_label; END IF;

  -- материал (если требуется): одна единица списывается
  IF v_mat IS NOT NULL THEN
     IF NOT EXISTS (SELECT 1 FROM player_inventory WHERE player_guid=p_player AND item_key=v_mat AND qty>0) THEN
        SET p_ok=0; SET p_msg='missing_material'; LEAVE proc_label;
     END IF;
     UPDATE player_inventory SET qty = qty - 1
       WHERE player_guid=p_player AND item_key=v_mat ORDER BY slot LIMIT 1;
     DELETE FROM player_inventory WHERE player_guid=p_player AND item_key=v_mat AND qty=0;
  END IF;

  -- резонанс ночи: если в любой зоне стоит darkness_flag — шанс расщепления растёт вдвое
  SELECT MAX(darkness_flag) INTO v_dark FROM fallen_state;
  IF v_dark = 1 THEN SET v_shatter = v_shatter * 2; END IF;
  SET v_roll = RAND()*100;

  START TRANSACTION;
    UPDATE player_soul_state SET soul_particles = soul_particles - v_cost WHERE player_guid=p_player;
    INSERT INTO soul_ledger (player_guid, delta, reason, ref_id) VALUES (p_player, -CAST(v_cost AS SIGNED), 'forge_craft', CONCAT('recipe_',p_recipe));
    IF v_roll < v_shatter THEN
      SET p_ok=1; SET p_msg='shattered';   -- частицы сгорели, предмет не создан
    ELSE
      INSERT INTO player_inventory (player_guid, item_key, qty) VALUES (p_player, v_out, 1)
        ON DUPLICATE KEY UPDATE qty = qty + 1;
      SET p_ok=1; SET p_msg='crafted';
    END IF;
  COMMIT;
END//
DELIMITER ;

-- ---- Авто-сбор частиц: привилегия сейдконы / экипировки ----
CREATE TABLE IF NOT EXISTS soul_autocollect (
  player_guid BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  radius_m DECIMAL(4,2) NOT NULL DEFAULT 3.00,
  enabled TINYINT(1) NOT NULL DEFAULT 1
) ENGINE=InnoDB;
