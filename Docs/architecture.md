# Архитектура игрового клиента MMORPG (UE6)

## 1. Общая схема

```
┌─────────────────────────── Client (UE6) ───────────────────────────┐
│  Launcher/Patcher → GameInstance                                    │
│      ├── MMOConnectionManager (Iris Net Driver client)              │
│      ├── MMOAuthService (HTTPS login → JWT → game-server handshake) │
│      ├── World Streaming (World Partition + Data Layers)            │
│      ├── Gameplay (input intents → server; state → render)          │
│      └── UI (UMG): Login HUD Inventory Chat Raid Auction            │
└──────────────┬────────────────────────────────┬─────────────────────┘
               │ UDP (Iris, unreliable+reliable) │ HTTPS (auth/billing)
        ┌──────▼──────┐                    ┌─────▼─────┐
        │ Game Server │◄── cluster bus ──►│ Auth/API  │
        │ (authoritative)                 │ Services  │
        └─────────────┘                    └───────────┘
```

**Ключевой принцип:** сервер — единственный источник истины. Клиент отправляет
намерения (движение, касты, действия с предметами) и отображает полученное
состояние. Любая клиентская логика — только предсказание (prediction) и
сглаживание (interpolation/smoothing).

## 2. Сетевой слой

- **Драйвер:** Iris (стандарт UE6), каналы Control/Voice, packed GUID serialization.
- **Тикрейт:** 30 Гц мир, движение — intent-based с клиентским предсказанием и
  серверными коррекциями (`SmoothCorrectionToServer`, hard teleport при desync > порога).
- **Reconnect:** экспоненциальный backoff (1→2→4→8→16 c, до 5 попыток), состояние
  `Reconnecting` в `EMMOConnectionState`, session resume по токену.
- **Полоса пропускания:** бюджет ~64 Кбит/с на соединение (DefaultEngine.ini),
  приоритетные каналы для боя (close relevance) против ambient NPC.

## 3. Аутентификация

1. Логин на auth-сервис по HTTPS (JWT, срок жизни короткий, refresh-токен отдельно).
2. Handshake с game-сервером: токен передаётся в URL подключения, сервер валидирует
   on-side и отвечает `EnterWorld`.
3. Пароль никогда не покидает HTTPS-канал; токены хранятся в secure storage.

## 4. Мир и стриминг

- **World Partition** для единого континента + **Data Layers** для инстансов
  (подземелья, рейды) — клиент грузит только актуальные грид-блоки.
- **Level streaming** лобби/загрузочных карт через TransitionMap.
- Межзонный travel — seamless где возможно, иначе loading screen с re-handshake.

## 5. Модули кода (Source/MMOClient)

| Директория | Назначение |
|---|---|
| `Net/` | типы протокола, ConnectionManager |
| `Auth/` | HTTPS-логин, session resume |
| `Character/` | клиентский персонаж, correction smoothing |
| `Game/` | GameMode (локальные карты Login/Lobby) |

## 6. Дальнейшие шаги

- [ ] Определить контракт protobuf/RPC-структур с бэкендом (Docs/proto/)
- [ ] Плагин патчера (manifest + diff-обновления, проверка хэшей)
- [ ] UI: экран логина (UMG) поверх `MMOAuthService`
- [ ] Боевая система: rollback-предсказание кастов
- [ ] Телеметрия/краш-репорты (CrashReportClient + свой конвейер)
- [ ] Античит: серверная валидация + клиентские детекторы как сигнал
