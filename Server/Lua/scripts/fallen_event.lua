--[[============================================================
  fallen_event.lua — «Падение» (The Fallen mechanic)
  Глобальный ивент: PvP-порог или таймер -> darkness_flag ->
  подмена spawn-пулов на Падших -> босс Сигрюн снимает флаг.
============================================================]]--

local FALLEN_CHECK_INTERVAL_MS = 30000   -- watchdog раз в 30с
local WINDOW_MINUTES = 10                -- скользящее окно PvP-убийств

--- Активация Падения в зоне
---@param zoneKey string
---@param poolId number
local function activateFallen(zoneKey, poolId)
    WorldDBQuery:Execute(string.format(
        "UPDATE fallen_state SET darkness_flag=1, pool_id=%d, started_at=NOW(), ends_at=NULL WHERE zone_key='%s'",
        poolId, zoneKey))
    -- движок: MapManager::SwapSpawnPools(zoneKey, poolId) — деспаун нормальных, спавн fallen
    SwapZoneSpawnPool(zoneKey, poolId)
    -- клиент UE6: пакет S2C_WorldState {darkness=1} -> патч шейдера неба
    BroadcastWorldStateToZone(zoneKey, "darkness", 1)
    BroadcastAnnounceToZone(zoneKey, "Небо рушится. Падшие идут за вашими душами.")
    PlayZoneAmbient(zoneKey, "fallen_sky_sting", loop = true)
end

--- Деактивация (зачистка босса либо истечение таймера)
---@param zoneKey string
local function deactivateFallen(zoneKey)
    local r = WorldDBQuery:Execute(string.format(
        "SELECT pool_id FROM fallen_state WHERE zone_key='%s' AND darkness_flag=1", zoneKey))
    local normalPool = nil
    if r then local row = r:NextRow(); if row then normalPool = tonumber(row[1]) end; r:CloseData() end

    WorldDBQuery:Execute(string.format(
        "UPDATE fallen_state SET darkness_flag=0, pvp_counter=0, ends_at=NOW() WHERE zone_key='%s'", zoneKey))
    SwapZoneSpawnPool(zoneKey, normalPool or 0)  -- возврат к обычному пулу
    BroadcastWorldStateToZone(zoneKey, "darkness", 0)
    BroadcastAnnounceToZone(zoneKey, "Сигрюн пала. Свет возвращается в " .. zoneKey .. ".")
end

--- Watchdog: проверка порогов
local function tickFallenCheck()
    -- 1) pvp_threshold зоны: счётчик ведёт SQL-триггер trg_pvp_count
    local q = WorldDBQuery:Execute(string.format(
        [[SELECT fs.zone_key, sp.pool_id FROM fallen_state fs
          JOIN spawn_pool sp ON sp.pool_key = CONCAT('pool_fallen_', fs.zone_key)
          WHERE fs.triggered_by='pvp_threshold' AND fs.darkness_flag=0
            AND fs.pvp_counter >= fs.threshold]], WINDOW_MINUTES))
    repeat local row = q and q:NextRow(); if row then
        activateFallen(row[1], tonumber(row[2]))
    until not row
    if q then q:CloseData() end

    -- 2) timer-зоны: threshold трактуется как интервал секунд от последней активации
    local t = WorldDBQuery:Execute([[SELECT zone_key FROM fallen_state
        WHERE triggered_by='timer' AND darkness_flag=0
          AND (started_at IS NULL OR started_at < NOW() - INTERVAL threshold SECOND)]])
    repeat local row = t and t:NextRow(); if row then
        local z = row[1]
        local p = WorldDBQuery:Execute("SELECT pool_id FROM spawn_pool WHERE pool_key LIKE '%fallen%' AND pool_key LIKE '%" .. z .. "%'")
        if p then local pr = p:NextRow(); if pr then activateFallen(z, tonumber(pr[1])) end; p:CloseData() end
    until not row
    if t then t:CloseData() end
end

--- Хук смерти моба: рост PvP-счётчика только для игроков->игроков уже в SQL;
--- здесь — сбор частиц с Падших (двойной drop в dark-фазе)
function OnCreatureKilled(p, c)
    if c:IsFallenVariant() and GetZoneDarkness(c:GetZoneKey()) == 1 then
        local base = c:GetSoulDropAmount()
        p:AddSoulParticles(math.floor(base * 1.5), "mob_kill_fallen_bonus", c:GetEntryKey())
    end
end

--- Босс-ивент: смерть Павшей Валькирии Сигрюн завершает Падение
function OnBossSigrynDied(p, boss)
    deactivateFallen(boss:GetZoneKey())
end

RegisterGlobalTimer(FALLEN_CHECK_INTERVAL_MS, tickFallenCheck)
RegisterEvent(7002, "OnCreatureKilled")
RegisterEvent(7003, "OnBossSigrynDied")
