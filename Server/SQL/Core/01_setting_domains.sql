-- ============================================================
-- «Огни душ Падших Валькирий» — Core schema: расы, классы, кланы
-- Target: MySQL 8.0 / InnoDB / utf8mb4
-- Де-WOW-изация: никаких faction/race/class ID Blizzard.
-- ============================================================

SET NAMES utf8mb4;

-- ---------- РАСЫ ----------
CREATE TABLE IF NOT EXISTS racial_def (
  race_key        VARCHAR(32)  NOT NULL PRIMARY KEY,   -- einherjar|huldra|leosalfar|duvalfar
  display_name    VARCHAR(64)  NOT NULL,
  description     TEXT,
  base_hp         INT UNSIGNED NOT NULL DEFAULT 100,
  base_essence_regen_pct DECIMAL(5,2) NOT NULL DEFAULT 0.00, -- % к регену эссенции
  passive_ability_id INT UNSIGNED NULL,                -- FK -> ability_def.id
  soul_capacity_mod  DECIMAL(5,2) NOT NULL DEFAULT 1.00,
  created_at      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

INSERT INTO racial_def (race_key, display_name, description, base_hp, base_essence_regen_pct, passive_ability_id, soul_capacity_mod) VALUES
 ('einherjar', 'Эйнхерии', 'Избранные воины, чья ярость растёт на грани смерти.', 120, 0.00, 90101, 1.00),
 ('huldra',    'Хульдры',   'Лесные духи; камуфляж вне боя, связь с рощами.',      95,  5.00, 90102, 1.10),
 ('leosalfar', 'Льёсальфары','Светлые альвы; ускоренный реген эссенции, детект нежити.', 90, 15.00, 90103, 1.20),
 ('duvalfar',  'Дювальфы',  'Тёмные альвы/гномы; резист магическому урону, ёмкий инвентарь кованого.', 100, 5.00, 90104, 0.90)
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name);

-- ---------- КЛАССЫ ----------
CREATE TABLE IF NOT EXISTS class_def (
  class_key       VARCHAR(32) NOT NULL PRIMARY KEY,    -- berserker|rune_caster|seidhkona
  display_name    VARCHAR(64) NOT NULL,
  resource_type   ENUM('saga_rage','blood','soul_particles') NOT NULL,
  primary_stat    VARCHAR(32) NOT NULL,
  combo_system    TINYINT(1) NOT NULL DEFAULT 0,       -- наличие комбо-приёмов
  can_cast_valkyrie_call TINYINT(1) NOT NULL DEFAULT 0,
  allowed_races_mask SET('einherjar','huldra','leosalfar','duvalfar') NOT NULL
) ENGINE=InnoDB;

INSERT INTO class_def (class_key, display_name, resource_type, primary_stat, combo_system, can_cast_valkyrie_call, allowed_races_mask) VALUES
 ('berserker',   'Берсеркер',      'saga_rage',      'might',     1, 0, 'einherjar,huldra,duvalfar'),
 ('rune_caster', 'Рунический маг', 'blood',          'insight',   0, 0, 'leosalfar,duvalfar,huldra'),
 ('seidhkona',   'Сейдкона',       'soul_particles', 'spirit',    0, 1, 'huldra,leosalfar')
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name);

-- ---------- ДОМА / КЛАНЫ (замена WOW-фракций) ----------
CREATE TABLE IF NOT EXISTS clan_def (
  clan_id         INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  clan_key        VARCHAR(32) NOT NULL UNIQUE,
  display_name    VARCHAR(64) NOT NULL,
  home_zone_key   VARCHAR(32) NOT NULL,               -- CUSTOM_PVP_ZONES key
  is_hostile_base TINYINT(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB;

INSERT INTO clan_def (clan_key, display_name, home_zone_key, is_hostile_base) VALUES
 ('hraesvelgr_clan',  'Клан Вранов',           'vindalr_plains', 0),
 ('iron_house',       'Дом Стали',             'forge_halls',    0),
 ('winterwolf_tribe', 'Племя Зимней Волчицы',  'frost_steppe',   0),
 ('fallen_valkyrie_order','Орден Падших Валькирий','asgard_fields',1)
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name);

-- Динамическая враждебность: матрица базовых отношений (-1..1)
CREATE TABLE IF NOT EXISTS clan_relation (
  clan_a INT UNSIGNED NOT NULL,
  clan_b INT UNSIGNED NOT NULL,
  base_score DECIMAL(4,2) NOT NULL DEFAULT 0.00,     -- -1.00 = blood feud, +1.00 = ally
  PRIMARY KEY (clan_a, clan_b),
  FOREIGN KEY (clan_a) REFERENCES clan_def(clan_id),
  FOREIGN KEY (clan_b) REFERENCES clan_def(clan_id)
) ENGINE=InnoDB;

-- ---------- СПОСОБНОСТИ (включая частиц-душ стоимость) ----------
CREATE TABLE IF NOT EXISTS ability_def (
  id              INT UNSIGNED NOT NULL PRIMARY KEY,
  ability_key     VARCHAR(64) NOT NULL UNIQUE,
  display_name    VARCHAR(64) NOT NULL,
  school          ENUM('physical','essence','rune','soul','beast') NOT NULL,
  resource_cost_type ENUM('none','saga_rage','blood','soul_particles','hp') NOT NULL DEFAULT 'none',
  resource_cost   INT UNSIGNED NOT NULL DEFAULT 0,
  cooldown_ms     INT UNSIGNED NOT NULL DEFAULT 0,
  gcd_ms          INT UNSIGNED NOT NULL DEFAULT 1000,
  range_m         DECIMAL(5,2) NOT NULL DEFAULT 5.00,
  aoe_cone_deg    DECIMAL(5,2) NULL,                  -- для swing-check на сервере
  requires_class  VARCHAR(32) NULL,
  flags           SET('channeled','on_death_target','pvp_only','night_resonance') NOT NULL DEFAULT '',
  lua_script      VARCHAR(128) NULL                   -- обработчик в Lua/scripts
) ENGINE=InnoDB;

-- Пассивики рас
INSERT INTO ability_def (id, ability_key, display_name, school, resource_cost_type, cooldown_ms, flags) VALUES
 (90101,'pass_fallen_rage','Ярость павшего','physical','none',0,''),
 (90102,'pass_grove_veil','Покров рощи','essence','none',0,''),
 (90103,'pass_light_ward','Светлый оберег','essence','none',0,''),
 (90104,'pass_forge_body','Кузнечная плоть','physical','none',0,'');

-- Зов валькирии (см. valkyrie_ressurrection_mechanic.sql)
INSERT INTO ability_def (id, ability_key, display_name, school, resource_cost_type, resource_cost, cooldown_ms, range_m, requires_class, flags, lua_script) VALUES
 (90001,'valkyrie_call','Зов валькирии','soul','soul_particles',30,120000,15.00,'seidhkona','on_death_target','valkyrie_call.lua')
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name);

-- ---------- ЧАСТИЦЫ ДУШ: журнал и капacity ----------
CREATE TABLE IF NOT EXISTS player_soul_state (
  player_guid BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  soul_particles INT UNSIGNED NOT NULL DEFAULT 0,
  soul_capacity  INT UNSIGNED NOT NULL DEFAULT 200,
  last_drop_zone VARCHAR(32) NULL,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS soul_ledger (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  player_guid BIGINT UNSIGNED NOT NULL,
  delta INT NOT NULL,                     -- +/- частицы
  reason ENUM('mob_kill','ability','forge_craft','death_tax','trade_evap','admin') NOT NULL,
  ref_id VARCHAR(64) NULL,                -- ability_key / item_key / trade session
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_player_time (player_guid, created_at)
) ENGINE=InnoDB;

-- ---------- СОСТОЯНИЕ «ПАДЕНИЯ» ----------
CREATE TABLE IF NOT EXISTS fallen_state (
  zone_key VARCHAR(32) NOT NULL PRIMARY KEY,
  pvp_counter INT UNSIGNED NOT NULL DEFAULT 0,
  darkness_flag TINYINT(1) NOT NULL DEFAULT 0,
  pool_id INT UNSIGNED NULL,              -- spawn_pool_override -> pool_fallen
  triggered_by ENUM('timer','pvp_threshold','boss_pull') NOT NULL DEFAULT 'pvp_threshold',
  threshold INT UNSIGNED NOT NULL DEFAULT 25,
  started_at TIMESTAMP NULL,
  ends_at TIMESTAMP NULL
) ENGINE=InnoDB;

INSERT INTO fallen_state (zone_key, threshold, triggered_by) VALUES
 ('asgard_fields', 25, 'pvp_threshold'),
 ('vindalr_plains', 40, 'pvp_threshold'),
 ('frost_steppe', 7200, 'timer');        -- таймер сек -> пороговое поле reinterpret как интервал

-- ---------- SPAWN POOLS («Падшие» версии мобов) ----------
CREATE TABLE IF NOT EXISTS spawn_pool (
  pool_id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  pool_key VARCHAR(64) NOT NULL UNIQUE,
  kind ENUM('normal','fallen') NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS spawn_pool_entry (
  pool_id INT UNSIGNED NOT NULL,
  creature_template_id INT UNSIGNED NOT NULL,
  weight DECIMAL(5,2) NOT NULL DEFAULT 1.00,
  PRIMARY KEY (pool_id, creature_template_id),
  FOREIGN KEY (pool_id) REFERENCES spawn_pool(pool_id)
) ENGINE=InnoDB;

-- ---------- ОГНИ ДУШ: рецепты крафта ----------
CREATE TABLE IF NOT EXISTS soul_forge_recipe (
  recipe_id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  output_item_key VARCHAR(64) NOT NULL,
  soul_cost INT UNSIGNED NOT NULL,
  material_item_key VARCHAR(64) NULL,
  night_resonance_bonus_pct DECIMAL(5,2) NOT NULL DEFAULT 25.00,
  shatter_chance_pct DECIMAL(5,2) NOT NULL DEFAULT 5.00,
  min_level TINYINT UNSIGNED NOT NULL DEFAULT 1
) ENGINE=InnoDB;

INSERT INTO soul_forge_recipe (output_item_key, soul_cost, material_item_key, night_resonance_bonus_pct) VALUES
 ('weapon_soul_eater', 150, 'ingot_wrought_iron', 30.00),
 ('amulet_ginnungagap', 90, NULL, 20.00),
 ('rune_blade_frost', 120, 'core_frostshard', 25.00);

-- ---------- ТОЧКИ ВОЗРОЖДЕНИЯ: ЗАЛ ПАВШИХ ----------
CREATE TABLE IF NOT EXISTS hall_of_fallen_loc (
  loc_id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  zone_key VARCHAR(32) NOT NULL,
  pos_x FLOAT NOT NULL, pos_y FLOAT NOT NULL, pos_z FLOAT NOT NULL,
  orientation FLOAT NOT NULL DEFAULT 0
) ENGINE=InnoDB;

INSERT INTO hall_of_fallen_loc (zone_key, pos_x, pos_y, pos_z, orientation) VALUES
 ('hall_primordial', 0.0, 0.0, 12.5, 1.57);
