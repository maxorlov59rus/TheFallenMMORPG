# MMO Client — Unreal Engine 6

Игровой клиент разрабатываемой MMORPG на **Unreal Engine 6**.

## Структура проекта

```
MMOClient.uproject          — корневой файл проекта UE6
Config/                     — конфигурация движка (Default*.ini)
Source/MMOClient/           — C++-код клиента (Game Module)
  Public/                   — публичные заголовки
  Private/                  — реализация
Content/                    — ассеты (уровни, материалы, Blueprint'ы)
Plugins/                    — плагины (сетевой слой, античит и т.д.)
Docs/                       — архитектурная документация
```

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
