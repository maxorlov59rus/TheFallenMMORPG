--[[============================================================
  valkyrie_call.lua — «Зов валькирии»: механика последнего шанса
  Таймер смерти 8с; revive на месте с 20% HP; timeout -> Зал Павших.
============================================================]]--

local DEATH_EVENT = 7010
local VALKYRIE_ABILITY_ID = 90001

--- Конфиг из БД (death_mechanic_config)
local function cfg(key, def)
    local r = WorldDBQuery:Execute("SELECT cfg_value FROM death_mechanic_config WHERE cfg_key='" .. key .. "'")
    local v = def
    if r then local row = r:NextRow(); if row then v = tonumber(row[1]) end; r:CloseData() end
    return v or def
end

--- Хук движка: HP==0 у игрока
function OnPlayerDied(p)
    CallProcedure("sp_player_died", p:GetGUID(), p:GetZoneKey())
    local secs = cfg("death_timer_seconds", 8)
    -- клиент UE6: экран «Последний шанс» + таймер-виджет
    p:SendDeathLastChance(secs)
    BroadcastWorldStateToNearby(p, "last_chance_begin", { guid = p:GetGUID(), ttl = secs })

    -- watchdog revival: если за secs секунд никто не кастовал Зов — hall
    AddUnitTimer(p, secs * 1000, function(unit)
        local q = WorldDBQuery:Execute(string.format(
            "SELECT state FROM player_death_state WHERE player_guid=%d", unit:GetGUID()))
        local st = nil
        if q then local row = q:NextRow(); if row then st = row[1] end; q:CloseData() end
        if st == "last_chance" then
            local ok = CallProcedure("sp_send_to_hall", unit:GetGUID())
            if ok == 1 then
                unit:TeleportToHallOfFallen()
                unit:ApplyAura(90201, cfg("battle_sorrow_debuff_sec", 60))  -- Тоска по бою
                unit:SendChat("Валькирии промолчали. Зал Павших принимает тебя.")
            end
        end
    end)
end

--- Каст Зова валькирии (цель — труп игрока в окне last_chance)
function OnValkyrieCall(caster, target)
    if not target:IsPlayer() or not target:IsDead() then
        caster:SendError("bad_target"); return
    end
    if caster:GetClassKey() ~= "seidhkona" then
        caster:SendError("class_restricted"); return
    end

    local ok, msg = CallProcedure("sp_valkyrie_call", caster:GetGUID(), target:GetGUID())
    if ok == 1 and msg == "revived_on_spot" then
        local hpPct = cfg("valkyrie_revive_hp_pct", 20)
        target:ReviveOnSpot(hpPct)   -- движок: снять death-state, выставить HP
        target:CancelDeathTimer()
        BroadcastWorldStateToNearby(target, "last_chance_end", { guid = target:GetGUID(), by = caster:GetName() })
        caster:SendChat("Ты услышан(а). " .. target:GetName() .. " возвращается с пылающими глазами.")
        PlayEffectAtTarget(caster, "valkyrie_bifrost_thread", target)
    else
        caster:SendError(msg)
        if msg == "window_expired" then
            caster:SendChat("Окно захлопнулось — душа уже ушла в Зал Павших.")
        end
    end
end

RegisterEvent(DEATH_EVENT, "OnPlayerDied")
RegisterAbility(VALKYRIE_ABILITY_ID, "OnValkyrieCall")
