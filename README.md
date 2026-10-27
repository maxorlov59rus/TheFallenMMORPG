# MMO Project — Unreal Engine 6 клиент + серверное ПО «Огни душ Падших Валькирий»

Игровой клиент разрабатываемой MMORPG (фэнтези-экшен, скандинавский сеттинг) на **Unreal Engine 6**.
Параллельно развивается серверная часть под Windows x64 с архитектурой уровня TrinityCore и **полной де-WOW-изацией** (`Server/`).

## Структура проекта

```
MMOClient.uproject          — корневой файл проекта UE6
Config/                     — конфигурация движка (Default*.ini)
Source/MMOClient/           — C++-код клиента (Game Module)
  Public/                   — публичные заголовки
  Private/                  — реализация
Content/                    — ассеты (уровни, материалы, Blueprint'ы)
Plugins/                    — плагины (сетевой слой, античит и т.д.)
Docs/                       — архитектурная документация клиента

Server/                     — серверное ПО «Огни душ Падших Валькирий» (Windows x64)
  Docs/                     — SERVER_ARCHITECTURE.md, PROTOCOL.md
  SQL/Core/                 — схемы доменов сеттинга (расы/классы/кланы/души)
  SQL/Patches/              — патчи геймплея: Fallen, Soul, Valkyrie
  Lua/scripts/              — soul_forge.lua, fallen_event.lua, valkyrie_call.lua
  Config/                   — ogni-world.conf (экшен-тайминги, server-authoritative бои)
  Tools/                    — check_wow_refs.py (CI-гейт де-WOW-изации)
```

## Сервер: быстрая навигация

- Архитектура и процесс компиляции: `Server/Docs/SERVER_ARCHITECTURE.md`
- Контракт с UE6-клиентом (msg_id registry): `Server/Docs/PROTOCOL.md`
- Порядок применения БД: `SQL/Core/01_setting_domains.sql` → `SQL/Patches/create_fallen_valkyries_racial_traits.sql` → `soul_system_items.sql` → `valkyrie_ressurrection_mechanic.sql`
- CI-гейт чистоты сеттинга: `python3 Server/Tools/check_wow_refs.py` (должен возвращать OK)

## Архитектура клиента (высокий уровень)

1. **Bootstrapper / Launcher** — обновление файлов, проверка целостности, запуск `UE6`-процесса с параметрами сервера.
2. **Network Layer** — Iris Net Driver (UE5+ / UE6), надёжные/ненадёжные каналы, состояние репликации, rollback для боевой системы.
3. **Persistence / Auth** — логин через токены (OAuth/JWT), session resume, reconnect с re-auth.
4. **Gameplay** — World Partition + Data Layers для стриминга зон MMO; ECS-подобные компоненты персонажа.
5. **UI** — UMG/Slate: HUD, инвентарь, чат, рейды, аукцион.
6. **Telemetry & Anti-cheat** — клиентская валидация только как фильтр; вся истина на сервере.

Подробнее см. `Docs/architecture.md`.

## Сборка

Требования: UE6 (Editor + ToolChain), Visual Studio 2022 / Rider, Git.

```bash
# Сгенерировать файлы проекта (Windows)
"<UE6>/Engine/Build/BatchFiles/GenerateProjectFiles.bat" -project="MMOClient.uproject" -game

# Сборка Development Editor
"<UE6>/Engine/Build/BatchFiles/Build.bat" MMOClientEditor Win64 Development -project="MMOClient.uproject" -waitmutex

# Сборка Shipping (клиент без редактора)
"<UE6>/Engine/Build/BatchFiles/Build.bat" MMOClient Win64 Shipping -project="MMOClient.uproject" -waitmutex
```

Запуск клиента к игровому серверу:

```bash
MMOClient.exe -server=game.example.com:7777 -log
```

## Конвенции кода

- Все сетевые структуры данных — в `Source/MMOClient/Public/Net/`, помечаются `USTRUCT(BlueprintType)` с `UPROPERTY()` для репликации.
- Никакой игровой логики, влияющей на баланс, на клиенте — клиент отображает состояние, полученное от сервера (server-authoritative).
- Имена: классы `FMMO*`/`AMMO*`/`UMMO*`, файлы в CamelCase.
