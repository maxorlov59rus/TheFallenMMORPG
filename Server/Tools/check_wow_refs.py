#!/usr/bin/env python3
"""check_wow_refs.py — CI-гейт де-WOW-изации.

Падает (exit 1), если в дереве Server/ встречаются идентификаторы Warcraft-ДНК:
названия локаций/фракций/мифологии Blizzard, WORLD_PVP_*, ссылки на DBC WOW.
Легальные скандинавские термины (valkyrie, einherjar...) — в allowlist.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # Server/

FORBIDDEN = [
    # Blizzard-топология и бренды
    r"\bAzshara\b", r"\bOutland\b", r"Twisting\s*Nether", r"Azeroth", r"\bKalimdor\b",
    r"\bEastern\s*Kingdoms\b", r"Draenor", r"WORLD_PVP_\w+",
    r"\bAlliance\b", r"\bHorde\b",                      # капс-термины фракций WOW в коде
    r"\bFel\b", r"\bthe\s*Light\b",                     # Скверна/Свет как доменные термины
    r"spell_dbc", r"item_template", r"creature_template.*WDB",
    r"TrinityCore", r"wowhead", r"\bwotlk\b", r"\bbfa\b", r"\bcc[0-9]?\b",
    r"Battle\.net", r"Blizzard",
]

# Контекстные исключения: строки с этим маркером не проверяются.
ALLOW_MARKERS = [
    "де-WOW", "WOW-ДНК", "ассимилятор", "без Blizzard", "НЕ из Blizzard",
    "вместо Blizzard", "аналог", "forbidden", "FORBIDDEN", "check_wow_refs",
    "WOW-", "Warcraft", "TrinityCore", "TC default", "TC-fork", "каркас TC",
]

# Блоки контекста: строки внутри таблиц/секций, заголовок которых содержит любой
# из этих маркеров, считаются де-WOW-документацией (перечисление удаляемого).
BLOCK_CONTEXT_MARKERS = ["де-WOW", "удал"]

def scan():
    hits = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in {".git", "build", "node_modules"} for part in path.parts):
            continue
        # сам файл линтера содержит запрещённые литералы — не проверяем его вовсе
        if path.name == "check_wow_refs.py":
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        in_allowed_block = False
        for i, line in enumerate(text.splitlines(), 1):
            low = line.lower()
            stripped = line.strip()
            # трек блоков markdown-таблиц: заголовок таблицы задаёт контекст
            if stripped.startswith("|") and "---" not in stripped:
                header = stripped
                if not in_allowed_block and any(m.lower() in header.lower() for m in BLOCK_CONTEXT_MARKERS):
                    in_allowed_block = True
            elif not stripped.startswith("|"):
                in_allowed_block = False

            if in_allowed_block:
                continue
            if any(m.lower() in low for m in ALLOW_MARKERS):
                continue
            for pat in FORBIDDEN:
                flags = re.IGNORECASE if pat.isupper() else 0
                if re.search(pat, line, flags=flags):
                    hits.append(f"{path.relative_to(ROOT.parent)}:{i}: [{pat}] {line.strip()[:120]}")
    return hits

if __name__ == "__main__":
    hits = scan()
    if hits:
        print("FAIL: найдены Warcraft-ассимиляторы (%d):" % len(hits))
        for h in hits:
            print("  " + h)
        sys.exit(1)
    print("OK: дерево Server/ чисто от WOW-ДНК.")
