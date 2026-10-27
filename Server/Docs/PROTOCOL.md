# PROTOCOL.md — бинарный протокол «Огни душ Падших Валькирий» ↔ UE6-клиент

Собственный протокол. Никаких Blizzard-opcodes — полная де-WOW-изация сетевого слоя (см. SERVER_ARCHITECTURE §1).

## Транспортные каналы
| Канал | Порт | Транспорт | Надёжность |
|---|---|---|---|
| Auth handshake | 443 | HTTPS/JSON | TLS |
| Session | 7777 | UDP, seq/ack поверх (наш reliable layer) | hybrid |
| Telemetry | 7778 | TCP | guaranteed |

Handshake: клиент `UMMOAuthService` получает JWT → `C2S_SessionHello{jwt}` → сервер валидирует с authserver → `S2C_SessionAccept{session_key, aoi_seed}` → переход `EMMOConnectionState::InWorld`.

## Фрейминг пакета (UDP)
```
[u16 magic 'OF'|'DV'][u8 ver][u16 msg_id][u32 seq][u8 flags(ack/reliable/compressed)][payload...]
```

## Message ID registry (выдержки)
### C2S (клиент → сервер)
| id | имя | поля | авторитет |
|---|---|---|---|
| 0x1001 | MoveUpdate | pos, vel, yaw, stamp_ms | интерполяция на сервере |
| 0x1002 | SwingBegin | weapon_id, dir, cone_deg, reach_m, stamp_ms | **результат считает сервер** (VMap/MMap LOS+collision, lag-rewind ≤200мс) |
| 0x1003 | CastAbility | ability_id, target_guid, aim_pos | сервер проверяет ресурс/cd/range |
| 0x1004 | ForgeCraft | recipe_id | → sp_soul_forge_craft |
| 0x1005 | CollectSoulDrop | drop_guid | радиус-валидация |
| 0x1006 | ClanPing | clan_key | репутация-запросы |

### S2C (сервер → клиент)
| id | имя | поля |
|---|---|---|
| 0x2001 | SpawnEntity | guid, kind, template_entry, pos, state |
| 0x2002 | DespawnEntity | guid |
| 0x2010 | SwingResult | attacker, victim, hit/miss/glanced, dmg, soul_gain |
| 0x2011 | AbilityAck/Nack | ability_id, reason |
| 0x2020 | WorldState | zone_key, key=val (darkness, last_chance_begin/end) |
| 0x2021 | DeathLastChance | ttl_sec |
| 0x2030 | SoulDelta | delta, reason — синхронизация UI-счётчика частиц |
| 0x2031 | ForgeMenu | recipes[] |
| 0x2040 | RepChange | clan_key, delta, new_score |

## Маппинг состояний
`EMMOConnectionState` (клиент Source/MMOClient/Net/MMONetTypes.h):
`Disconnected→(нет сессии)`, `Connecting→SessionHello sent`, `Authenticating→JWT check`, `InWorld→AOI active`, `Reconnecting→backoff`.

## Версионирование
msg_id стабильны; breaking-change только bump `ver` + graceful reject `S2C_ProtocolMismatch` → патчер клиента.
