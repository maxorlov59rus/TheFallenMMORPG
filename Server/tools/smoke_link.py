#!/usr/bin/env python3
"""E2E smoke: клиент-эмулятор -> auth login -> world handshake -> swing/rep. Выход 0 = связка живая."""
import sys, os, json, socket, struct, urllib.request
sys.path.insert(0, os.path.dirname(__file__))
from ogni_wire import *

# 1) HTTP логин (как UMMOAuthService::Login)
req = json.dumps({'username':'testvala','password':'vala123','client_build':'ogni-p0-dev'}).encode()
r = urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:4443/api/v1/login', data=req,
                        headers={'Content-Type':'application/json'}), timeout=5)
resp = json.loads(r.read())
assert resp.get('token'), resp
print('[1] auth login OK, jwt received, world:', resp['world_host'], resp['world_port'])

# 2) UDP SessionHello (как BeginConnect + HandleSuccessfulConnection)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.settimeout(5)
jwt = resp['token'].encode()
hello = struct.pack('<H', len(jwt)) + jwt
sock.sendto(pack_frame(Msg.SESSION_HELLO, 1, hello, FLAG_RELIABLE), ('127.0.0.1', 7777))
data, _ = sock.recvfrom(2048)
ver, mid, seq, fl, body = unpack_frame(data)
assert mid == Msg.SESSION_ACCEPT, hex(mid)
key, seed = dec_session_accept(body)
assert len(key) == 32 and seed == 0xA05EED
print('[2] world handshake OK, session_key', key.hex()[:12]+'...', 'aoi_seed', hex(seed))

# 3) SwingBegin -> серверный SwingResult (клиент НЕ судья ударам)
sock.sendto(pack_frame(Msg.SWING_BEGIN, 2, enc_swing_begin(7,(1,0,0),60.0,2.5,int(time.time()*1000))), ('127.0.0.1',7777))
data,_ = sock.recvfrom(2048)
_, mid, _, _, body = unpack_frame(data)
assert mid == Msg.SWING_RESULT
att, vic, outcome, dmg, soul = struct.unpack('<QQBII', body)
assert outcome==1 and dmg==17 and soul==3, (dmg,soul)
print(f'[3] server-authoritative swing OK: victim={vic:#x} dmg={dmg} soul_gain={soul}')

# 4) ClanPing -> RepChange
sock.sendto(pack_frame(Msg.CLAN_PING, 3, b'hraesvelgr_clan'.ljust(16,b'\0')), ('127.0.0.1',7777))
data,_ = sock.recvfrom(2048)
_, mid, _, _, body = unpack_frame(data)
assert mid == Msg.REP_CHANGE and json.loads(body)['clan']=='hraesvelgr_clan'
print('[4] clan rep OK')

# 5) негатив: фальшивый JWT -> ProtocolMismatch
bad = pack_frame(Msg.SESSION_HELLO, 1, struct.pack('<H',4)+b'junk')
s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s2.settimeout(5)
s2.sendto(bad, ('127.0.0.1',7777))
data,_ = s2.recvfrom(2048)
assert unpack_frame(data)[1] == Msg.PROTOCOL_MISMATCH
print('[5] bad-jwt rejection OK')
print('SMOKE_LINK_PASS')
