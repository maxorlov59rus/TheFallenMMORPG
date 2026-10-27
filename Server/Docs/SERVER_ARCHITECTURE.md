# «Огни душ Падших Валькирий» — Серверное ПО

**Windows x64 · архитектура уровня TrinityCore · полная де-WOW-изация · фэнтези-экшен**

Документ описывает серверную часть, разрабатываемую параллельно с игровым клиентом UE6 (`/MMOClient`).

---

## 1. Принцип де-WOW-изации

TrinityCore берётся как **каркас инженерных решений**, а не как контент:

| Слой | Что переиспользуется (дизайн TC) | Что удаляется безвозвратно (де-WOW-ингредиенты) |
|---|---|---|
| Сеть | WorldSocket/WorldSession, крипто handshake, пакетный pipeline | Двоичные протоколы Blizzard, opcode-таблицы WOW |
| Мир | Map/MapInstance/Grids, таймеры обновления, спавн-система | ID карт Azeroth, DBC-структуры WOW |
| Данные | Схема auth/world (patterns), SQL-updater | Таблицы `spell_dbc`, `item_template` в формате Blizzard |
| Скрипты | ELuna/C++ ScriptBind-подход | ~7000 скриптов BfA/WotLK |
| Геймплей | Паттерны AI (SMI), threat, auras | Маны/Энергия/Ярость WOW, Ауры WOW, фракции Орда/Альянс |

**Юридическая гигиена:** в репозитории запрещено упоминать `WORLD_PVP_*`, `Azshara`, `Outland`, `TwistingNether`, имена Blizzard. Все зоны — нейтральный модуль `CUSTOM_PVP_ZONES`. CI содержит линтер `Tools/check_wow_refs.py`, падающий на любых ассимиляторах Warcraft-ДНК.

### Протокол вместо Blizzard-opcodes
Клиент UE6 и сервер общаются через **собственный бинарный протокол поверх UDP (IRIS-совместимые каналы)** + HTTPS для auth. Общая схема сериализации описана в `Docs/PROTOCOL.md` (см. ниже §6).

---

## 2. Доменные модели сеттинга

### 2.1 Расы (таблица `racial_def`)
| Раса | Ключ | Пассивика |
|---|---|---|
| Эйнхерии | `einherjar` | +10% урона при HP < 30% («Ярость павшего») |
| Хульдры | `huldra` | Лесной камуфляж вне боя; +репутация с духами рощ |
| Льёсальфары | `leosalfar` | +15% к регену эссенции; светлая аура (детект нежити) |
| Дювальфы | `duvalfar` | Резистность к магическому урону; +ёмкость инвентаря кованых предметов |

### 2.2 Классы (таблица `class_def`)
| Класс | Ключ | Ресурс | Ядро механики |
|---|---|---|---|
| Берсеркер | `berserker` | Ярость-Сага (накапливается от полученных ударов) | Комбо-приёмы, отменяемые «Боевым кличем»; transition в берсерк-стан |
| Рунический маг | `rune_caster` | **Собственное HP → руны** | Заклинания вырезаются ценой здоровья; крит-прок «Руна крови» |
| Сейдкона | `seidhkona` | **Частицы душ** | Шёпот мёртвых: канализация собранных душ; восстание трупов как временных слуг; «Зов валькирии» |

### 2.3 Фракции = Дома/Кланы
Таблица `clan_def`: `hraesvelgr_clan` (Клан Вранов), `iron_house` (Дом Стали), `winterwolf_tribe` (Племя Зимней Волчицы). Враждебность **динамическая**:

```
reputation(player, clanB) -= k * kills(clanB mobs)   // убийства мобов B
reputation(player, clanA) += k * kills(clanB mobs)   // растёт репутация врага B
```

Модуль `ClanReputationSystem` пересобирает `FactionTemplate`-эквивалент в рантайме: отношение PvP/PvE игрока к любой цели вычисляется из его текущих значений репутации, а не из статичной таблицы.

### 2.4 Магия = управление эссенцией
Отказ от Света/Скверны. Школа магии `essence` имеет три состояния потока: **Искра → Пламя → Пепел**. Способности хранят `soul_particles_cost` в `ability_def` (см. SQL Core).

---

## 3. Система Частиц Душ (Soul Particles)

Новый ресурс первого класса, заменяет ману для сейдконы и частично для рунического мага.

**Цикл:**
1. **Сбор.** Моб при смерти создаёт `SoulDrop` (spawns table) с количеством частиц по формуле `base(soul_quality) * level_diff_factor`. Подбор — через loot-событие или авто-сбор в радиусе (настраивается per-класс).
2. **Хранение.** Игрок имеет `soul_capacity` (раса+класс+экипировка дают модификаторы). Избыток сгорает («Пепел»).
3. **Расход.**
   - способности (SQL `ability_def.soul_cost`);
   - крафт на Огнях душ (§5);
   - торговля между игроками только через ритуал «Передача огня» (односторонняя, с комиссией-испарением 10%).
4. **Санкция.** Смерть игрока теряет 5% текущего запаса — «долг перед Гиннунгагапом».

Сервер ведёт журнал `soul_ledger` (delta-лог каждой транзакции) — античит + экономика.

---

## 4. Экшен-требования к ядру (Windows x64)

### 4.1 Server-authoritative hit detection
В отличие от классического TC (клиент = истина в ударах), здесь:

- каждый взмах оружия = событие `SwingBegin(dir, cone, reach)` от клиента **без** результата;
- сервер проверяет через **VMap** (geometry) и **MMap** (проходимость): траекторию оружия как swept-cone, line-of-sight, коллизию цели;
- результат (`hit/miss/glanced`) сервер диктует клиенту. Нагрузка на CPU ×2–3 → см. §4.3.

### 4.2 Конфиг worldserver (`Config/ogni-world.conf`)
Ключевые значения под экшен (полный файл в репо):

```ini
MapUpdateInterval      = 60      ; мс, вместо 100 — плавность позиционирования
PlayerSaveInterval     = 360     ; сек (6 мин) — смерть в экшене не должна терять прогресс
Compression            = 1       ; zlib/zstd на потоке движения
MaxPacketsPerSecond    = 800     ; action-heavy трафик
MovementAuthMode       = full    ; server-side collision+LOS для каждого swing
LuaEngine              = 1
```

### 4.3 Производительность
- MSVC v143, `/arch:AVX2 /O2 /GL /fp:fast` для слоёв физики и заклинаний (+15–20% против MinGW).
- SIMD-библиотека `Math::Simd` для swept-cone тестов и many-body обновлений NPC.
- Профилирование: ETW + perf counters, бюджет: ≤ 3 мс тик карты на 200 игроков.

---

## 5. Системы мира

### 5.1 «Падение» (The Fallen mechanic)
Глобальный ивент-процесс сервера:

- таблица `fallen_state` (zone_id, pvp_counter, darkness_flag, pool_id);
- триггеры: таймер ИЛИ порог PvP-убийств в локации (Поля Асгарда → **Виндельн**);
- при активации `FallenEventController` меняет флаг → клиент получает `WorldState.Darkness` (шейдер неба через патч UE-клиента);
- **MapManager-модификация:** динамическая подмена creature-spawn-пулов (`spawn_pool_override`) — обычные мобы десpawn'ятся, спавнятся «Падшие» версии из `pool_fallen`;
- деактивация: зачистка босса «Павшая Валькирия» из специального инстанса.

### 5.2 Огни душ (Soul Forges)
Узлы = точка респауна + крафт-станция одновременно:
1. игрок активирует Огонь (interact event);
2. сервер проверяет `inventory.soul_particles >= recipe.cost`;
3. Lua-скрипт `soul_forge.lua` конвертирует частицы → предмет (`weapon_soul_eater` и др.), минуя профессионную систему WOW;
4. резонанс: крафт в тёмное время (Fallen-флаг) даёт +редкость с шансом расщепления узла.

### 5.3 Смерть и Валгалла («последний шанс»)
```
HP==0 -> DeathTimer(8s) [движок: Unit::Die() override]
  ├─ кто-то кастует "Valkyrie Call" (ability 90001) -> revive on spot, HP=20%, soul_tax=0
  └─ timeout -> телепорт в Зал Павших (Hall of the Fallen = WorldSafeLocs-аналог),
                soul_loss=5%, debuff "Тоска по бою" 60s
```
`ZovValkyrii` доступен только сейдконе; кулдаун привязан к запасу частиц кастера.

---

## 6. Протокол взаимодействия с UE6-клиентом

| Канал | Транспорт | Содержимое |
|---|---|---|
| Auth | HTTPS (443) | логин/JWT (клиент `UMMOAuthService` ↔ `authserver`) |
| Session | UDP (7777) | движение, swing events, AOI-спавны, мир-стейты |
| Reliable | поверх UDP (seq/ack) | чат, лут,交易, квесты, репутация |
| Telemetry | TCP (7778) | heartbeat, anti-cheat samples |

Маппинг: клиентский `EMMOConnectionState` ↔ серверные `WorldSession::State`. Спецификация пакетов — `Docs/PROTOCOL.md` (заготовка создана).

---

## 7. Процесс сборки (Windows x64)

```powershell
# 0. Инструментарий
winget install Microsoft.VisualStudio.2022.Community  # Desktop C++, Windows SDK
choco install cmake ninja mysql openssl boost        # Boost 1.82+, OpenSSL 3.x, MySQL 8.0

# 1. Каркас (TC-fork как template, обнулённый)
git clone https://github.com/<org>/ogni-core-server Server/Core   # submodule/pinned fork
cd Server\Core

# 2. CMake — строго Win64 + AVX2
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_CXX_FLAGS="/arch:AVX2 /fp:fast" ^
      -DTOOLSET_VERSION=14.3

# 3. Сборка RelWithDebInfo (оптимизация + стектрейсы падений)
cmake --build build --config RelWithDebInfo --parallel %NUMBER_OF_PROCESSORS%

# 4. Контент из UE6-клиента (НЕ из Blizzard!)
mapextractor.exe --client ../../MMOClient --out ./data   # vmaps/mmaps/gt эквиваленты наших .ognimap
copy .\data\* .\build\RelWithDebInfo\data\

# 5. Первый запуск — авто-миграция БД
authserver.exe   # создаёт realmd-аналог: auth_mongo? -> MySQL `ogni_auth`
worldserver.exe  # применяет SQL/Core/*.sql затем SQL/Patches/*.sql через Updater

# 6. Патчи геймплея (порядок фиксирован)
mysql -u root -p ogni_world < SQL/Patches/create_fallen_valkyries_racial_traits.sql
mysql -u root -p ogni_world < SQL/Patches/soul_system_items.sql
mysql -u root -p ogni_world < SQL/Patches/valkyrie_ressurrection_mechanic.sql
```

**CI-гейты:** компиляция MSVC x64; юнит-тесты `Math::Simd`; `check_wow_refs.py` (0 совпадений); smoke: старт worldserver на тестовой БД + handshake с headless-клиентом.

---

## 8. Структура репозитория (серверная часть)

```
Server/
├── Docs/            — этот документ, PROTOCOL.md, DB_SCHEMA.md
├── SQL/Core/        — схемы рас/классов/кланов/способностей
├── SQL/Patches/     — три обязательных патча геймплея
├── Lua/scripts/     — soul_forge.lua, fallen_event.lua, valkyrie_call.lua
├── Config/          — ogni-world.conf, ogni-auth.conf
└── Tools/           — check_wow_refs.py, pack_soul_drop.py
```

Ядро (`Server/Core`) подключается как pinned submodule форка с обнулённым контентом — сам код TC-наследника живёт в отдельном репо во избежание лицензионных проблем.
