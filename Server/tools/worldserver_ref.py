#!/usr/bin/env python3
"""
ogni-worldserver (референс-прототип связки) — UDP :7777 wire-протокол.
handshake: C2S SessionHello{jwt} -> validate у authserver TCP:3724 -> S2C SessionAccept{session_key, aoi_seed}
далее: MoveUpdate (echo rate-limit), SwingBegin -> сервер считает SwingResult (упрощённый cone-check без VMap в прототипе).
Прод-реализация: C++/Win64 поверх ядра (см. SERVER_ARCHITECTURE.md); этот файл = поведенческая спецификация + smoke-тест.
"""
import os, sys, json, socket, struct, time, threading
import re

sys.path.insert(0, os.path.dirname(__file__))
from ogni_wire import (Msg, FLAG_RELIABLE, pack_frame, unpack_frame,
                      dec_move_update, dec_swing_begin, enc_session_accept, enc_world_state)

UDP_PORT   = int(os.environ.get('OGNI_WORLD_PORT', '7777'))
AUTH_HOST  = os.environ.get('OGNI_AUTH_HOST', '127.0.0.1')
AUTH_TCP   = int(os.environ.get('OGNI_AUTH_TCP_PORT', '3724'))
MAX_PKTS_PER_SEC = 800  # синхронно с ogni-world.conf
DB_HOST = os.environ.get('OGNI_DB_HOST', '127.0.0.1')
DB_USER = os.environ.get('OGNI_DB_USER', 'ogni')
DB_PW   = os.environ.get('OGNI_DB_PW', 'ogni_dev_pw')
MAX_CHARS_PER_ACCOUNT = 5

sessions = {}          # endpoint -> dict(seq_in, seq_out, claims, key)
sess_lock = threading.Lock()

def db(dbname):
    import mysql.connector
    return mysql.connector.connect(host=DB_HOST, user=DB_USER, password=DB_PW, database=dbname)

def auth_validate(jwt: str):
    try:
        with socket.create_connection((AUTH_HOST, AUTH_TCP), timeout=3) as s:
            s.sendall((json.dumps({'op':'validate','jwt':jwt})+'\n').encode())
            return json.loads(s.recv(65536))
    except Exception as e:
        return {'ok': False, 'error': f'auth_unreachable:{e}'}

# ---------- персонажная линия (редактор создания) ----------
def get_templates_summary():
    c = db('ogni_characters'); cur = c.cursor(dictionary=True)
    cur.execute("SELECT template_key, race_key, class_key, display_name, description, starting_zone "
                "FROM char_template WHERE active=1")
    rows = cur.fetchall(); c.close(); return rows

def get_templates_full():
    c = db('ogni_characters'); cur = c.cursor(dictionary=True)
    cur.execute("SELECT * FROM char_template WHERE active=1")
    rows = cur.fetchall()
    for r in rows:
        if isinstance(r.get('appearance_schema'), str): r['appearance_schema'] = json.loads(r['appearance_schema'])
        if isinstance(r.get('base_stats'), str): r['base_stats'] = json.loads(r['base_stats'])
    c.close(); return rows

def validate_name(name: str):
    if not re.fullmatch(r'[A-Z][a-zA-Z]{1,23}', name or ''):
        return 'CHAR_BAD_NAME'
    try:
        c = db('ogni_characters'); cur = c.cursor(dictionary=True)
        cur.execute("SELECT forbidden_words FROM name_policy LIMIT 1")
        row = cur.fetchone(); c.close()
        if row and name.lower() in json.loads(row['forbidden_words']):
            return 'CHAR_FORBIDDEN_NAME'
    except Exception:
        pass
    return None

def handle_char_message(sock, addr, sess, msg_id, body):
    """Возвращает True, если сообщение обработано в персонажной линии."""
    claims = sess['claims']; acc = claims['sub']
    def send(mid, payload_dict):
        sock.sendto(pack_frame(mid, sess['seq_out'], json.dumps(payload_dict, ensure_ascii=False).encode()), addr)
        sess['seq_out'] += 1

    if msg_id == Msg.CHAR_LIST_REQ:
        c = db('ogni_characters'); cur = c.cursor(dictionary=True)  # владение персонажами — зона мира (миграция 06)
        cur.execute("SELECT id,name,race_key,class_key,level FROM characters WHERE account_id=%s AND deleted_at IS NULL", (acc,))
        roster = cur.fetchall(); c.close()
        send(Msg.CHAR_LIST, {'characters': roster, 'templates': get_templates_summary(), 'max_chars': MAX_CHARS_PER_ACCOUNT})
        return True
    if msg_id == Msg.CHAR_TEMPLATES:  # C2S-запрос полного schema для редактора переиспользует тот же msg_id как триггер
        send(Msg.CHAR_TEMPLATES, {'templates': get_templates_full()})
        return True
    if msg_id == Msg.CHAR_CREATE:
        try:
            req = json.loads(body)
            tkey, name, appearance = str(req['template_key']), str(req['name']), req.get('appearance', {})
        except Exception:
            send(Msg.CHAR_CREATE_OK, {'error': 'CHAR_BAD_JSON'}); return True
        err = validate_name(name)
        if err: send(Msg.CHAR_CREATE_OK, {'error': err}); return True
        c = db('ogni_characters'); cur = c.cursor(dictionary=True)
        cur.execute("SELECT * FROM char_template WHERE template_key=%s AND active=1", (tkey,))
        tpl = cur.fetchone(); c.close()
        if not tpl: send(Msg.CHAR_CREATE_OK, {'error': 'CHAR_UNKNOWN_TEMPLATE'}); return True
        # серверная валидация внешности по allowed_values схемы
        schema = tpl['appearance_schema']
        if isinstance(schema, str): schema = json.loads(schema)
        for slot, val in appearance.items():
            allowed = schema.get('slots', {}).get(slot)
            if allowed is None or val not in allowed:
                send(Msg.CHAR_CREATE_OK, {'error': f'CHAR_BAD_APPEARANCE:{slot}'}); return True
        # лимит + уникальность имени (атомарно через INSERT с UNIQUE на имя)
        c = db('ogni_characters'); cur = c.cursor(dictionary=True)
        cur.execute("SELECT COUNT(*) n FROM characters WHERE account_id=%s AND deleted_at IS NULL", (acc,))
        if cur.fetchone()['n'] >= MAX_CHARS_PER_ACCOUNT:
            c.close(); send(Msg.CHAR_CREATE_OK, {'error': 'CHAR_LIMIT'}); return True
        try:
            cur.execute("INSERT INTO characters (account_id,name,race_key,class_key,level) VALUES (%s,%s,%s,%s,1)",
                        (acc, name, tpl['race_key'], tpl['class_key']))
            cid = cur.lastrowid; c.commit()
        except Exception:
            c.close(); send(Msg.CHAR_CREATE_OK, {'error': 'CHAR_NAME_TAKEN'}); return True
        c2 = db('ogni_characters'); cur2 = c2.cursor()
        cur2.execute("INSERT INTO character_appearance (character_id, template_id, appearance_json) VALUES (%s,%s,%s)",
                     (cid, tpl['template_id'], json.dumps(appearance)))
        cur2.execute("INSERT INTO character_snapshot (character_id, base_hp, resource_type, base_resource, stats_json, starting_zone) "
                     "VALUES (%s,%s,%s,%s,%s,%s)",
                     (cid, tpl['base_hp'], tpl['resource_type'], tpl['base_resource'],
                      json.dumps(tpl['base_stats'] if isinstance(tpl['base_stats'], dict) else json.loads(tpl['base_stats'])),
                      tpl['starting_zone']))
        c2.commit(); c2.close(); c.close()
        send(Msg.CHAR_CREATE_OK, {'character_id': cid, 'name': name, 'template_key': tkey})
        print(f'[world] char created acc={acc} id={cid} name={name} tpl={tkey}', flush=True)
        return True
    if msg_id == Msg.CHAR_ENTER_WORLD:
        try:
            cid = int(json.loads(body)['character_id'])
        except Exception:
            send(Msg.WORLD_READY, {'error': 'CHAR_BAD_JSON'}); return True
        c = db('ogni_characters'); cur = c.cursor(dictionary=True)
        cur.execute("SELECT id,name FROM characters WHERE id=%s AND account_id=%s AND deleted_at IS NULL", (cid, acc))
        ch = cur.fetchone(); c.close()
        if not ch: send(Msg.WORLD_READY, {'error': 'CHAR_NOT_OWNED'}); return True
        c = db('ogni_characters'); cur = c.cursor(dictionary=True)
        cur.execute("SELECT * FROM character_snapshot WHERE character_id=%s", (cid,))
        snap = cur.fetchone(); c.close()
        if not snap: send(Msg.WORLD_READY, {'error': 'CHAR_NO_SNAPSHOT'}); return True
        zone = snap['starting_zone']
        pos = [10.0, 20.0, 0.0]
        c = db('ogni_world'); cur = c.cursor()
        cur.execute("REPLACE INTO character_state (character_id, zone_key, pos_x, pos_y, pos_z, hp, max_hp, resource_type, resource_value, level) "
                    "VALUES (%s,%s,%s,%s,%s,%s,%s,%s,%s,1)",
                    (cid, zone, pos[0], pos[1], pos[2], snap['base_hp'], snap['base_hp'], snap['resource_type'], snap['base_resource']))
        cur.execute("REPLACE INTO active_session (session_id, character_id, world_server_id, udp_endpoint) "
                    "VALUES (%s,%s,1,%s)", (claims['jti'], cid, f'{addr[0]}:{addr[1]}'))
        c.commit(); c.close()
        sess['character_id'] = cid
        send(Msg.WORLD_READY, {'character_id': cid, 'name': ch['name'], 'zone_key': zone,
                               'pos': pos, 'hp': snap['base_hp'], 'max_hp': snap['base_hp'],
                               'resource_type': snap['resource_type'], 'resource': snap['base_resource'],
                               'stats': snap['stats_json'] if isinstance(snap['stats_json'], dict) else json.loads(snap['stats_json'])})
        print(f'[world] enter-world acc={acc} char={cid} zone={zone}', flush=True)
        return True
    return False

def handle_frame(sock, data, addr):
    try:
        ver, msg_id, seq, flags, body = unpack_frame(data)
    except ValueError:
        return  # мусор/спуфинг — дроп без ответа
    with sess_lock:
        sess = sessions.get(addr)
        if msg_id == Msg.SESSION_HELLO:
            jwt_len = struct.unpack('<H', body[:2])[0]
            jwt = body[2:2+jwt_len].decode()
            res = auth_validate(jwt)
            if not res.get('ok'):
                sock.sendto(pack_frame(Msg.PROTOCOL_MISMATCH, 0, b'\x01'), addr)
                return
            claims = res['claims']
            sess = dict(seq_in=seq, seq_out=0, claims=claims, key=os.urandom(32), last=time.time(), cnt=0)
            sessions[addr] = sess
            sock.sendto(pack_frame(Msg.SESSION_ACCEPT, sess['seq_out'], enc_session_accept(sess['key'], 0xA05EED)), addr)
            sess['seq_out'] += 1
            print(f'[world] session open acc={claims["sub"]} from {addr}', flush=True)
            return
        if sess is None:
            return  # без сессии ничего не принимаем
        sess['cnt'] += 1
        if sess['cnt'] > MAX_PKTS_PER_SEC:
            return
        sess['seq_in'] = seq
        if handle_char_message(sock, addr, sess, msg_id, body):
            return
        if msg_id == Msg.MOVE_UPDATE:
            mv = dec_move_update(body)  # серверный авторитет: в проде здесь collision/anti-cheat
        elif msg_id == Msg.SWING_BEGIN:
            sw = dec_swing_begin(body)
            # упрощённый server-side hit-check: фиксированный «тренировочный» victim guid=0x42, dmg из weapon_id
            result = struct.pack('<QQBII', sess['claims']['sub'], 0x42, 1, 10 + (sw['weapon_id'] % 20), 3)  # outcome=hit, dmg, soul_gain=3
            sock.sendto(pack_frame(Msg.SWING_RESULT, sess['seq_out'], result), addr)
            sess['seq_out'] += 1
        elif msg_id == Msg.CLAN_PING:
            payload = json.dumps({'clan': 'hraesvelgr_clan', 'score': 0}).encode()
            sock.sendto(pack_frame(Msg.REP_CHANGE, sess['seq_out'], payload), addr)
            sess['seq_out'] += 1

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', UDP_PORT))
    print(f'[world] UDP :{UDP_PORT} ready', flush=True)
    while True:
        data, addr = sock.recvfrom(2048)
        threading.Thread(target=handle_frame, args=(sock, data, addr), daemon=True).start()

if __name__ == '__main__':
    main()
