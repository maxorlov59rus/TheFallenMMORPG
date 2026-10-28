# Пайплайн контента: UE 5.8 + Quixel Bridge + Fab

## 1. Версии движка — dual-rail
| Рельс | EngineAssociation | Назначение |
|---|---|---|
| **Основной (prod-альфа)** | `5.8` | Стабильная ветка: сборка, стенд-тесты с ogni-auth/world, запись альфа-видео |
| Preview | `6.0` (отдельный чек-аут `MMOClient.uproject.ue6`) | Ранняя оценка Iris/WorldPartition в UE6; не блокирует альфу |

Переезд на 6.0 — после того как серверный C++ стек (Win64) примет Iris-фрейминг без изменений контракта PROTOCOL.md.

## 2. Плагины (включены в MMOClient.uproject)
### QuixelBridge (Enabled, Win64)
- Мега-сканы окружения: скалы Фимбульвертира, рунические камни Огней душ, интерьер Зала Павших.
- Конвенция имён: `T_<Category>_<ScanName>_<Variant>` (напр. `T_Env_RuneStone_A`).
- Экспорт мешей только через **Megascans Plugin → Import to Project**, в `/Game/Content/Megascans/...`; вручную папки не трогать (Bridge пишет манифест).
- LOD policy: статика окружения — Nanite (r.SkinCache.CompileShaders=1 уже включён); скелетные персонажи — классические LOD, Nanite-skeletal НЕ использовать до замера CPU-профиля боёв 40v40.

### FabImporter (Enabled, Optional, Win64)
- Каталог ассетов: анимационные наборы (берсеркер/сейдкона), VFX-паки, аудио.
- Лицензионный гейт: в Fab помечать лицензию CC0/UE-EULA; ассеты «Editor only» в пайплайн не допускаются (проверка при импорте скриптом Tools/fab_import_check.ps1 — TODO).
- Импортированные папки — `/Game/Content/Fab/<AssetPackage>/`, переименование сразу под контракт Content/README.md.

## 3. Механика интеграции
- Оба плагина — Editor-only; в Shipping-билд не попадают (EnabledInShipping=false по умолчанию у плагинов моста) — размер клиента не растёт.
- Контракт имён ассетов, на который ссылается код (M_WorldValidated, NI_Soul_Gather/Forge, BodyPreset-спрайты) — в `Content/README.md`; Quixel/Fab-ассеты маппятся на контракт через DataAsset `DA_AssetRegistry` (TODO).
- .uasset из-под Bridge/Fab коммитятся в Git LFS (см. .gitattributes); raw-экспорты (glb/fbx) в репозиторий не кладём.

## 4. Порядок действий художника
1. EPIC Launcher → UE 5.8 → открыть MMOClient.uproject.
2. Window → Quixel Bridge: импорт сцены → выбрать локацию (Windfall Plains = vindalr_plains) → Import.
3. Fab (Window → Fab или fab.com): скачать → Project Browser → Add to Project → переименовать под контракт.
4. Запустить `Tools/content_validation.sh` (проверка имён/материалов против Content/README.md) — зелёный → PR.
