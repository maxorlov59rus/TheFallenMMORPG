#!/usr/bin/env python3
"""
ogni-worldserver (референс-прототип связки) — UDP :7777 wire-протокол.
handshake: C2S SessionHello{jwt} -> validate у authserver TCP:3724 -> S2C SessionAccept{session_key, aoi_seed}
далее: MoveUpdate (echo rate-limit), SwingBegin -> сервер считает SwingResult (упрощённый cone-check без VMap в прототипе).
Прод-реализация: C++/Win64 поверх ядра (см. SERVER_ARCHITECTURE.md); этот файл = поведенческая спецификация + smoke-тест.
"""
import os, sys, json, socket, struct, time, threading

sys.path.insert(0, os.path.dirname(__file__))
from ogni_wire import (Msg, FLAG_RELIABLE, pack_frame, unpack_frame,
                      dec_move_update, dec_swing_begin, enc_session_accept, enc_world_state)

UDP_PORT   = int(os.environ.get('OGNI_WORLD_PORT', '7777'))
AUTH_HOST  = os.environ.get('OGNI_AUTH_HOST', '127.0.0.1')
AUTH_TCP   = int(os.environ.get('OGNI_AUTH_TCP_PORT', '3724'))
MAX_PKTS_PER_SEC = 800  # синхронно с ogni-world.conf

sessions = {}          # endpoint -> dict(seq_in, seq_out, claims, key)
sess_lock = threading.Lock()

def auth_validate(jwt: str):
    try:
        with socket.create_connection((AUTH_HOST, AUTH_TCP), timeout=3) as s:
            s.sendall((json.dumps({'op':'validate','jwt':jwt})+'\n').encode())
            return json.loads(s.recv(65536))
    except Exception as e:
        return {'ok': False, 'error': f'auth_unreachable:{e}'}

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
