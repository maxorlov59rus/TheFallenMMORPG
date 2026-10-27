--[[============================================================
  soul_forge.lua — Огни душ: крафт из Частиц Душ
  Вызывается worldserver на событие GO_SoulForge_Activate.
  Де-WOW: не использует профессионную систему, только sp_soul_forge_craft.
============================================================]]--

local SOUL_FORGE_EVENT = 7001  -- кастомный event id (см. Docs/PROTOCOL.md)

-- Кеширование рецептов локально в Lua-стейте движка (инвалидируется reload-командой)
local recipeCache = nil
local function getRecipes()
    if not recipeCache then
        recipeCache = {}
        local r = CharQueryEntry:Execute("SELECT recipe_id, output_item_key, soul_cost FROM soul_forge_recipe")
        repeat local row = r:NextRow(); if row then
            recipeCache[tonumber(row[1])] = { item = row[2], cost = tonumber(row[3]) }
        end until not row
        r:CloseData()
    end
    return recipeCache
end

local function logSoulDelta(guid, delta, reason, ref)
    WorldDBQuery:Execute(string.format(
        "INSERT INTO soul_ledger (player_guid, delta, reason, ref_id) VALUES (%d, %d, '%s', '%s')",
        guid, delta, reason, ref or ""))
end

---@param p Player
---@param go GameObject (soul_forge_node)
function OnSoulForgeInteract(p, go)
    local nodeZone = go:GetZoneKey()
    local recipes = getRecipes()

    -- UI-меню: клиент UE6 получает список рецептов пакетом S2C_ForgeMenu
    local list = {}
    for id, rec in pairs(recipes) do
        table.insert(list, { id = id, item = rec.item, cost = rec.cost })
    end
    p:SendForgeMenu(SOUL_FORGE_EVENT, list)
end

---@param p Player  @клиент прислал C2S_ForgeCraft {recipe_id}
function OnSoulForgeCraftRequest(p, recipeId)
    local rec = getRecipes()[recipeId]
    if not rec then p:SendError("no_recipe"); return end

    local have = p:GetSoulParticles()
    if have < rec.cost then
        p:SendError("insufficient_souls")
        p:SendChat("Для «" .. rec.item .. "» нужно " .. rec.cost .. " частиц, у тебя " .. have .. ".")
        return
    end

    -- material-проверка и транзакция — на стороне MySQL-процедуры (атомарно)
    local ok, msg = CallProcedure("sp_soul_forge_craft", p:GetGUID(), recipeId)
    if ok == 1 then
        if msg == "crafted" then
            p:AddItem(rec.item, 1)
            logSoulDelta(p:GetGUID(), -rec.cost, "forge_craft", "recipe_" .. recipeId)
            p:SendChat("Огонь принял твои частицы. Кован рождён: " .. rec.item)
            BroadcastSoundToNearby(p, "soul_forge_success", 25.0)
        elseif msg == "shattered" then
            p:SendChat("Частицы рассыпались пеплом... узел раскололся.")
            BroadcastSoundToNearby(p, "soul_forge_shatter", 25.0)
        end
    else
        p:SendError(msg)
    end
end

RegisterEvent(SOUL_FORGE_EVENT, "OnSoulForgeInteract", "OnSoulForgeCraftRequest")
