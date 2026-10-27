#!/usr/bin/env python3
"""E2E smoke персонажной линии: login -> hello -> CharListReq -> CharTemplates -> CharCreate(валидные/невалидные) -> EnterWorld.
Выход 0 = редактор персонажа на стенде жив. Зеркаляет UMMOCharacterEditorSubsystem."""
import sys, os, json, socket, struct, time, urllib.request
sys.path.insert(0, os.path.dirname(__file__))
from ogni_wire import *

BASE_USER = os.environ.get('OGNI_TEST_USER', 'testvala')
def login(user, pw):
    req = json.dumps({'username':user,'password':pw,'client_build':'ogni-p0-dev'}).encode()
    r = urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:4443/api/v1/login', data=req,
                            headers={'Content-Type':'application/json'}), timeout=5)
    return json.loads(r.read())

resp = login(BASE_USER, 'vala123')
assert resp.get('token'), resp
print('[1] auth login OK (account %s)' % BASE_USER)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.settimeout(5)
jwt = resp['token'].encode()
sock.sendto(pack_frame(Msg.SESSION_HELLO, 1, struct.pack('<H', len(jwt)) + jwt, FLAG_RELIABLE), ('127.0.0.1', 7777))
data,_ = sock.recvfrom(65536)
assert unpack_frame(data)[1] == Msg.SESSION_ACCEPT
print('[2] world handshake OK')

seq = 2
def send_json(mid, obj):
    global seq
    sock.sendto(pack_frame(mid, seq, json.dumps(obj).encode()), ('127.0.0.1', 7777)); seq += 1
def recv_json():
    while True:
        data,_ = sock.recvfrom(65536)
        ver, mid, sq, fl, body = unpack_frame(data)
        if body[:1] == b'{':
            return mid, json.loads(body)
        # бинарный кадр — пропускаем (например ack)

# 3) ростер
send_json(Msg.CHAR_LIST_REQ, {})
mid, roster = recv_json()
assert mid == Msg.CHAR_LIST, hex(mid)
assert 'characters' in roster and 'templates' in roster and roster['max_chars'] == 5
print(f"[3] CharList OK: {len(roster['characters'])} chars, {len(roster['templates'])} templates summary")

# 4) полные шаблоны для редактора
send_json(Msg.CHAR_TEMPLATES, {})
mid, tpl = recv_json()
assert mid == Msg.CHAR_TEMPLATES and len(tpl['templates']) >= 8, len(tpl.get('templates',[]))
t = next(x for x in tpl['templates'] if x['template_key']=='einherjar_berserker')
slots = t['appearance_schema']['slots']
assert 'body_preset' in slots and 'vithr' in slots['body_preset']
print(f"[4] CharTemplates OK: {len(tpl['templates'])} шаблонов; слоты einherjar: {list(slots)}")

# 5) негативы валидации
uniq = f"Smok{int(time.time())%100000}"
for bad_name, expect in [("x", 'CHAR_BAD_NAME'), ("Odin", 'CHAR_FORBIDDEN_NAME')]:
    # сервер нормализует регистр при forbidden-проверке -> шлём 'Odin' (regex-ok), ожидим FORBIDDEN
    send_json(Msg.CHAR_CREATE, {'template_key':'einherjar_berserker','name':bad_name,'appearance':{}})
    mid, res = recv_json()
    assert mid == Msg.CHAR_CREATE_OK and res.get('error','').startswith(expect), (bad_name, res)
print('[5] name-policy rejects OK (BAD_NAME, FORBIDDEN)')

send_json(Msg.CHAR_CREATE, {'template_key':'einherjar_berserker','name':uniq,'appearance':{'body_preset':'not_a_valid_preset'}})
mid, res = recv_json()
assert res.get('error','').startswith('CHAR_BAD_APPEARANCE'), res
print('[6] appearance slot validation OK:', res['error'])

# 6) легальное создание
app = {k: v[0] for k, v in slots.items()}
send_json(Msg.CHAR_CREATE, {'template_key':'einherjar_berserker','name':uniq,'appearance':app})
mid, res = recv_json()
assert mid == Msg.CHAR_CREATE_OK and 'character_id' in res, res
cid = res['character_id']
print(f"[7] CharCreate OK: id={cid} name={res['name']}")

# 7) вход в мир
send_json(Msg.CHAR_ENTER_WORLD, {'character_id': cid})
mid, wr = recv_json()
assert mid == Msg.WORLD_READY and wr.get('zone_key')=='vindalr_plains' and wr['hp']==wr['max_hp']==120, wr
assert wr['resource_type']=='saga_rage' and wr['stats']['might']==14
print(f"[8] WorldReady OK: zone={wr['zone_key']} hp={wr['hp']}/{wr['max_hp']} res={wr['resource_type']} pos={wr['pos']}")

# 8) БД-сторона: снапшот и состояние записаны
import mysql.connector
c = mysql.connector.connect(host='127.0.0.1', user='ogni', password='ogni_dev_pw', database='ogni_characters')
cur = c.cursor(dictionary=True)
cur.execute("SELECT template_id FROM character_appearance WHERE character_id=%s", (cid,)); row = cur.fetchone()
cur.execute("SELECT base_hp, resource_type FROM character_snapshot WHERE character_id=%s", (cid,)); snap = cur.fetchone()
c.close(); assert row and snap and snap['base_hp']==120
c = mysql.connector.connect(host='127.0.0.1', user='ogni', password='ogni_dev_pw', database='ogni_world')
cur = c.cursor(dictionary=True)
cur.execute("SELECT zone_key, hp, max_hp FROM character_state WHERE character_id=%s", (cid,)); st = cur.fetchone()
c.close(); assert st and st['zone_key']=='vindalr_plains'
print('[9] DB rows OK: character_appearance/snapshot + ogni_world.character_state')
print('SMOKE_CHARACTERS_PASS')
