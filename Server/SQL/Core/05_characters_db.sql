-- ============================================================
-- 05_characters_db.sql — БД персонажей (ogni_characters) и шаблоны редактора.
-- Разделение ответственности:
--   ogni_characters.characters — владелец (account_id), имя, soft-delete (миграция 06)
--   ogni_characters.*         — внешность/конфиг из редактора (character_template_json) + снапшот стартовых статов
--   ogni_world.character_state— runtime-состояние (позиция, HP, ресурс)
-- Порядок применения: 01 -> 02 -> 03 -> 04 -> 05 -> 06 (перенос владения) -> патчи.
-- ============================================================
SET NAMES utf8mb4;

CREATE DATABASE IF NOT EXISTS ogni_characters CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE ogni_characters;

-- ---------- ШАБЛОНЫ РЕДАКТОРА ПЕРСОНАЖА ----------
-- Один шаблон = пресет расы+класса с допустимым набором черт внешности.
-- Клиент рендерит превью по appearance_slots; сервер валидирует выбор по allowed_values.
CREATE TABLE IF NOT EXISTS char_template (
  template_id     INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  template_key    VARCHAR(32) NOT NULL UNIQUE,          -- 'einherjar_berserker' и т.д.
  race_key        VARCHAR(32) NOT NULL,                 -- -> ogni_world.racial_def
  class_key       VARCHAR(32) NOT NULL,                 -- -> ogni_world.class_def
  display_name    VARCHAR(64) NOT NULL,
  description     TEXT NULL,
  starting_zone   VARCHAR(32) NOT NULL DEFAULT 'vindalr_plains',
  -- JSON-контракт для редактора UE6 (CharacterEditor widget): слоты и допустимые значения
  appearance_schema JSON NOT NULL,
  -- стартовые атрибуты (серверный авторитет; клиент только отображает)
  base_hp         INT UNSIGNED NOT NULL,
  resource_type   ENUM('saga_rage','blood','soul_particles') NOT NULL,
  base_resource   INT UNSIGNED NOT NULL DEFAULT 100,
  base_stats      JSON NOT NULL,                        -- {"might":10,"insight":8,...}
  active          TINYINT(1) NOT NULL DEFAULT 1,
  INDEX idx_race_class (race_key, class_key)
) ENGINE=InnoDB;

-- Шаблоны по всем легальным комбинациям раса×класс из allowed_races_mask (01_setting_domains.sql):
-- berserker: einherjar,huldra,duvalfar | rune_caster: leosalfar,duvalfar,huldra | seidhkona: huldra,leosalfar
INSERT INTO char_template
 (template_key, race_key, class_key, display_name, description, starting_zone, appearance_schema, base_hp, resource_type, base_resource, base_stats)
VALUES
 ('einherjar_berserker','einherjar','berserker','Эйнхерий-берсеркер',
  'Избранный воин. Ярость саги растёт на грани смерти.', 'vindalr_plains',
  '{"slots":{"body_preset":["vithr","skald","shieldmaiden"],"hair":["bound","loose","shaved_ritual"],"face_scar":["none","wolf","raven","runed"],"eye_glow":["none","ember","frost"],"armor_style":["chain","leather","fur"]},"presets":["default","hound_of_odin"]}',
  120,'saga_rage',100,'{"might":14,"insight":6,"spirit":6,"vigor":10,"presence":7}'),
 ('huldra_berserker','huldra','berserker','Хульдра-берсеркер',
  'Лесной дух в ярости: камуфляж вне боя, берёзовый гнев в бою.', 'mirkwood_hollow',
  '{"slots":{"body_preset":["birch","fern","moss"],"hair":["vine_woven","wild","hidden"],"face_markings":["bark","sap","none"],"ears":["pointed_long","pointed_short"],"eyes":["amber","green"],"armor_style":["bark","leather","leafmail"]},"presets":["default","grove_warden"]}',
  95,'saga_rage',100,'{"might":10,"insight":9,"spirit":8,"vigor":8,"presence":9}'),
 ('duvalfar_berserker','duvalfar','berserker','Дювальф-берсеркер',
  'Тёмный альв-кузнец: стойкость подкованного тела.', 'irondeep_hold',
  '{"slots":{"body_preset":["anvil","forgeborn","stoneback"],"hair":["topknot","hooded","braided_beard"],"skin_tone":["ash","umber","obsidian"],"eyes":["molten","dim"],"tattoos":["forge_runes","none"],"armor_style":["plate_studded","leather","chain"]},"presets":["default","ember_smith"]}',
  100,'saga_rage',100,'{"might":12,"insight":7,"spirit":5,"vigor":12,"presence":7}'),
 ('leosalfar_rune_caster','leosalfar','rune_caster','Льёсальфар-рунический маг',
  'Светлый альв: руны горят собственной кровью мага.', 'alfheim_terrace',
  '{"slots":{"body_preset":["lumen","dawn","veil"],"hair":["silverflow","crown_braids","short_lumen"],"eyes":["starwhite","gold"],"rune_branding":["forehead","cheek","hands","none"],"auricle":["long","plumed"],"robe_style":["lightweave","warpmantle"]},"presets":["default","dawnscript"]}',
  90,'blood',100,'{"might":5,"insight":15,"spirit":10,"vigor":6,"presence":9}'),
 ('duvalfar_rune_caster','duvalfar','rune_caster','Дювальф-рунический маг',
  'Глубинные руны: кровь темных альв питает каменное письмо.', 'irondeep_hold',
  '{"slots":{"body_preset":["anvil","forgeborn","stoneback"],"hair":["topknot","hooded","braided_beard"],"skin_tone":["ash","umber","obsidian"],"eyes":["molten","dim"],"rune_branding":["forearms","scalp","none"],"robe_style":["stonewrap","sootweave"]},"presets":["default","deep_script"]}',
  100,'blood',100,'{"might":6,"insight":13,"spirit":9,"vigor":9,"presence":8}'),
 ('huldra_rune_caster','huldra','rune_caster','Хульдра-рунический маг',
  'Руны из живой древесины; кровь — сок рощи.', 'mirkwood_hollow',
  '{"slots":{"body_preset":["birch","fern","moss"],"hair":["vine_woven","wild","hidden"],"face_markings":["bark","sap","none"],"ears":["pointed_long","pointed_short"],"rune_branding":["throat","shoulder","none"],"robe_style":["leafscroll","barkwrap"]},"presets":["default","root_scribe"]}',
  95,'blood',100,'{"might":5,"insight":14,"spirit":10,"vigor":7,"presence":9}'),
 ('huldra_seidhkona','huldra','seidhkona','Хульдра-сейдкона',
  'Проводница душ среди живых рощ.', 'mirkwood_hollow',
  '{"slots":{"body_preset":["birch","fern","moss"],"hair":["vine_woven","wild","hidden"],"face_markings":["bark","sap","none"],"ears":["pointed_long","pointed_short"],"eyes":["amber","green","void"],"staff_sigil":["rowan","emberwood","bone"]},"presets":["default","mist_caller"]}',
  95,'soul_particles',80,'{"might":4,"insight":9,"spirit":16,"vigor":6,"presence":10}'),
 ('leosalfar_seidhkona','leosalfar','seidhkona','Льёсальфар-сейдкона',
  'Свет, что зовёт павших обратно: Зов валькирии.', 'alfheim_terrace',
  '{"slots":{"body_preset":["lumen","dawn","veil"],"hair":["silverflow","crown_braids","short_lumen"],"eyes":["starwhite","gold","void"],"auricle":["long","plumed"],"staff_sigil":["feather","lightthread","bone"]},"presets":["default","dawn_summoner"]}',
  90,'soul_particles',80,'{"might":4,"insight":8,"spirit":16,"vigor":6,"presence":11}')
ON DUPLICATE KEY UPDATE display_name=VALUES(display_name), appearance_schema=VALUES(appearance_schema);

-- ---------- СОСТОЯНИЕ ПЕРСОНАЖА В РЕДАКТОРЕ (через character_creation_session) ----------
-- Черты выбранного персонажа фиксируются при создании; json = итоговый вид редактора.
CREATE TABLE IF NOT EXISTS character_appearance (
  character_id    BIGINT UNSIGNED NOT NULL PRIMARY KEY,  -- = ogni_characters.characters.id
  template_id     INT UNSIGNED NOT NULL,
  appearance_json JSON NOT NULL,                         -- выбранные значения по слотам схемы
  created_at      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (template_id) REFERENCES char_template(template_id)
) ENGINE=InnoDB;

-- Стартовый снапшот (одноразово копируется в ogni_world.character_state при первом входе в мир)
CREATE TABLE IF NOT EXISTS character_snapshot (
  character_id   BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  base_hp        INT UNSIGNED NOT NULL,
  resource_type  ENUM('saga_rage','blood','soul_particles') NOT NULL,
  base_resource  INT UNSIGNED NOT NULL,
  stats_json     JSON NOT NULL,
  starting_zone  VARCHAR(32) NOT NULL,
  created_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Ограничения на имена (редактор + серверная валидация)
CREATE TABLE IF NOT EXISTS name_policy (
  min_len  TINYINT UNSIGNED NOT NULL DEFAULT 2,
  max_len  TINYINT UNSIGNED NOT NULL DEFAULT 24,
  regex_ok VARCHAR(128) NOT NULL DEFAULT '^[A-Z][a-zA-Z]{1,23}$',
  forbidden_words JSON NOT NULL COMMENT 'мат/склонение к NPC/спойлеры кланов'
) ENGINE=InnoDB;
INSERT INTO name_policy (min_len,max_len,regex_ok,forbidden_words) VALUES
 (2,24,'^[A-Z][a-zA-Z]{1,23}$','["odin","vala","sigrid","fallen","wlkv","admin","gm","test"]')
ON DUPLICATE KEY UPDATE regex_ok=VALUES(regex_ok);

-- Права сервисного пользователя на новую БД
GRANT SELECT, INSERT, UPDATE, DELETE, EXECUTE ON ogni_characters.* TO 'ogni'@'localhost';

-- Миграция существующего dev-персонажа Sigrid под шаблон (если база уже наполнена 04_)
INSERT IGNORE INTO character_appearance (character_id, template_id, appearance_json)
SELECT c.id, t.template_id, '{"body_preset":"vithr","hair":"bound","face_scar":"raven","eye_glow":"ember","armor_style":"chain"}'
FROM characters c JOIN char_template t ON t.template_key='einherjar_berserker' WHERE c.name='Sigrid';

INSERT IGNORE INTO character_snapshot (character_id, base_hp, resource_type, base_resource, stats_json, starting_zone)
SELECT ca.character_id, t.base_hp, t.resource_type, t.base_resource, t.base_stats, t.starting_zone
FROM character_appearance ca JOIN char_template t ON t.template_id = ca.template_id;
