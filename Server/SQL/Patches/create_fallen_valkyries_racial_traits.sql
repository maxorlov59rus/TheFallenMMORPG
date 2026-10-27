-- ============================================================
-- Патч 1/3: create_fallen_valkyries_racial_traits.sql
-- «Падение» как системный процесс + расовые трейты скандинавского сеттинга
-- Зависимости: SQL/Core/01_setting_domains.sql
-- ============================================================

-- ---- Расовые трейты как аурные способности (замена WOW-трейтов) ----
INSERT INTO ability_def (id, ability_key, display_name, school, resource_cost_type, flags, lua_script) VALUES
 (90110,'trait_einherjar_last_stand','Ярость павшего (+10% урона при HP<30%)','physical','none','','fallen_rage_aura.lua'),
 (90111,'trait_huldra_grove_cloak','Покров рощи (камфляж вне боя)','essence','none','','grove_cloak.lua'),
 (90112,'trait_leosalfar_essence_flow','Светлый поток (+15% реген эссенции)','essence','none','',NULL),
 (90113,'trait_duvalfar_forge_skin','Кузнечная плоть (-12% маг. урона)','rune','none','',NULL)
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name);

-- Привязка трейтов к расам через таблицу соответствия
CREATE TABLE IF NOT EXISTS racial_trait_map (
  race_key VARCHAR(32) NOT NULL,
  ability_id INT UNSIGNED NOT NULL,
  slot ENUM('passive','active') NOT NULL DEFAULT 'passive',
  PRIMARY KEY (race_key, ability_id),
  FOREIGN KEY (race_key) REFERENCES racial_def(race_key),
  FOREIGN KEY (ability_id) REFERENCES ability_def(id)
) ENGINE=InnoDB;

INSERT INTO racial_trait_map (race_key, ability_id) VALUES
 ('einherjar',90110), ('huldra',90111), ('leosalfar',90112), ('duvalfar',90113);

-- ---- СИСТЕМА «ПАДЕНИЯ»: пулы Падших мобов ----
INSERT INTO spawn_pool (pool_key, kind) VALUES
 ('pool_normal_asgard','normal'),
 ('pool_fallen_asgard','fallen'),
 ('pool_fallen_vindalr','fallen')
ON DUPLICATE KEY UPDATE kind=VALUES(kind);

-- Шаблоны существ: нейтральные creature_template без WOW-ID
CREATE TABLE IF NOT EXISTS creature_template (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  entry_key VARCHAR(64) NOT NULL UNIQUE,
  display_name VARCHAR(96) NOT NULL,
  type ENUM('beast','draugr','jotun','alfar','valkyrie','fenrirkin') NOT NULL,
  level TINYINT UNSIGNED NOT NULL,
  hp_base INT UNSIGNED NOT NULL,
  soul_quality DECIMAL(5,2) NOT NULL DEFAULT 1.00,   -- множитель дропа частиц (§3 архитектуры)
  clan_id INT UNSIGNED NULL,
  is_fallen_variant TINYINT(1) NOT NULL DEFAULT 0,
  fallen_of_entry VARCHAR(64) NULL,                  -- для Падших версий: ключ оригинала
  FOREIGN KEY (clan_id) REFERENCES clan_def(clan_id)
) ENGINE=InnoDB;

INSERT INTO creature_template (entry_key, display_name, type, level, hp_base, soul_quality, clan_id, is_fallen_variant) VALUES
 ('wolf_frost',      'Морозный волк',       'beast',    12, 800,   1.0, NULL, 0),
 ('draugr_bog',      'Болотный драугр',     'draugr',   18, 1500,  1.4, NULL, 0),
 ('jotun_frost',     'Ингвальд-инейцевый ётун','jotun', 30, 5200,  2.2, NULL, 0),
 ('valkyrie_guard',  'Валькирия-страж',     'valkyrie', 40, 9000,  3.0, NULL, 0);

-- Падшие версии (spawn-pool override цели)
INSERT INTO creature_template (entry_key, display_name, type, level, hp_base, soul_quality, clan_id, is_fallen_variant, fallen_of_entry) VALUES
 ('wolf_fallen',     'Падший морозный волк','beast',    15, 1200,  2.0, NULL, 1, 'wolf_frost'),
 ('draugr_fallen',   'Падший драугр',       'draugr',   22, 2400,  2.6, NULL, 1, 'draugr_bog'),
 ('jotun_fallen',    'Падший инейцевый ётун','jotun',   36, 8800,  4.0, NULL, 1, 'jotun_frost'),
 ('valkyrie_fallen_boss','Павшая Валькирия Сигрюн','valkyrie',50, 42000, 8.0, NULL, 1, 'valkyrie_guard');

-- Наполнение пулов
INSERT INTO spawn_pool_entry (pool_id, creature_template_id, weight)
SELECT p.pool_id, c.id, 1.0 FROM spawn_pool p JOIN creature_template c
 ON (p.pool_key='pool_normal_asgard' AND c.entry_key IN ('wolf_frost','draugr_bog','jotun_frost','valkyrie_guard'))
 OR (p.pool_key='pool_fallen_asgard' AND c.entry_key IN ('wolf_fallen','draugr_fallen','jotun_fallen'));

-- Босс ивента привязан отдельно: pull-событие деактивирует darkness_flag
UPDATE fallen_state SET pool_id = (SELECT pool_id FROM spawn_pool WHERE pool_key='pool_fallen_asgard')
WHERE zone_key='asgard_fields';

-- ---- Пороги PvP -> активация (движок: ClanReputationSystem hook) ----
CREATE TABLE IF NOT EXISTS pvp_kill_events (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  attacker_guid BIGINT UNSIGNED NOT NULL,
  victim_guid BIGINT UNSIGNED NOT NULL,
  zone_key VARCHAR(32) NOT NULL,
  occurred_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_zone_time (zone_key, occurred_at)
) ENGINE=InnoDB;

-- Триггер счётчика: worldserver читает скользящее окно 10 минут
DELIMITER //
CREATE TRIGGER trg_pvp_count AFTER INSERT ON pvp_kill_events
FOR EACH ROW BEGIN
  UPDATE fallen_state fs
     SET fs.pvp_counter = (SELECT COUNT(*) FROM pvp_kill_events e
                            WHERE e.zone_key = NEW.zone_key
                              AND e.occurred_at > NOW() - INTERVAL 10 MINUTE)
   WHERE fs.zone_key = NEW.zone_key AND fs.triggered_by='pvp_threshold';
END//
DELIMITER ;
