"""
«Огни душ Падших Валькирий» — референс wire-протокола (Python, для тестов и codegen).
Зеркалит Docs/PROTOCOL.md + msg_registry. Используется smoke-тестами связки
authserver -> worldserver -> клиентский эмулятор.
"""
import struct, hashlib, json, os, time

MAGIC = b'\x4f\x46'          # 'OF'
VER   = 1

# msg_id registry (синхронно с SQL/Core/04_wire_contracts.sql)
class Msg:
    MOVE_UPDATE       = 0x1001
    SWING_BEGIN       = 0x1002
    CAST_ABILITY      = 0x1003
    FORGE_CRAFT       = 0x1004
    COLLECT_SOUL_DROP = 0x1005
    CLAN_PING         = 0x1006
    SPAWN_ENTITY      = 0x2001
    DESPAWN_ENTITY    = 0x2002
    SWING_RESULT      = 0x2010
    ABILITY_ACK       = 0x2011
    WORLD_STATE       = 0x2020
    DEATH_LAST_CHANCE = 0x2021
    SOUL_DELTA        = 0x2030
    FORGE_MENU        = 0x2031
    REP_CHANGE        = 0x2040
    SESSION_HELLO     = 0x2F02   # C2S handshake (jwt)
    SESSION_ACCEPT    = 0x2F00   # S2C
    PROTOCOL_MISMATCH = 0x2F01
    # --- персонажная линия: редактор создания персонажа ---
    CHAR_LIST_REQ     = 0x3001   # C2S {}
    CHAR_LIST         = 0x3002   # S2C json: roster + templates summary
    CHAR_TEMPLATES    = 0x3003   # S2C json: полный appearance_schema для редактора
    CHAR_CREATE       = 0x3010   # C2S json {template_key,name,appearance}
    CHAR_CREATE_OK    = 0x3011   # S2C json {character_id,...} | error
    CHAR_ENTER_WORLD  = 0x3012   # C2S json {character_id}
    WORLD_READY       = 0x3013   # S2C json {zone,pos,hp,...}

FLAG_ACK       = 0x01
FLAG_RELIABLE  = 0x02
FLAG_COMPRESSED= 0x04

def pack_frame(msg_id: int, seq: int, payload: bytes, flags: int = FLAG_RELIABLE) -> bytes:
    return MAGIC + struct.pack('<BHIB', VER, msg_id, seq, flags) + payload

HDR_LEN = 10  # magic(2) + ver(1) + msg_id(2) + seq(4) + flags(1)
def unpack_frame(buf: bytes):
    if len(buf) < HDR_LEN or buf[:2] != MAGIC:
        raise ValueError('bad magic/version')
    ver, msg_id, seq, flags = struct.unpack('<BHIB', buf[2:HDR_LEN])
    return ver, msg_id, seq, flags, buf[HDR_LEN:]

# --- типизированные тела (по schema_json из БД) ---
def enc_move_update(pos, vel, yaw, stamp_ms):
    return struct.pack('<fffffffQ', pos[0], pos[1], pos[2], vel[0], vel[1], vel[2], yaw, stamp_ms)

def dec_move_update(p):
    x,y,z,vx,vy,vz,yaw,stamp = struct.unpack('<fffffffQ', p)
    return dict(pos=(x,y,z), vel=(vx,vy,vz), yaw=yaw, stamp_ms=stamp)

def enc_swing_begin(weapon_id, direction, cone_deg, reach_m, stamp_ms):
    return struct.pack('<IfffffQ', weapon_id, direction[0], direction[1], direction[2], cone_deg, reach_m, stamp_ms)

def dec_swing_begin(p):
    wid, dx, dy, dz, cone, reach, stamp = struct.unpack('<IfffffQ', p)
    return dict(weapon_id=wid, dir=(dx,dy,dz), cone_deg=cone, reach_m=reach, stamp_ms=stamp)

def enc_session_accept(session_key: bytes, aoi_seed: int):
    return struct.pack('<H', len(session_key)) + session_key + struct.pack('<I', aoi_seed)

def dec_session_accept(p):
    (n,) = struct.unpack('<H', p[:2]); key = p[2:2+n]; (seed,) = struct.unpack('<I', p[2+n:])
    return key, seed

def enc_world_state(zone_key: str, kv: dict):
    body = json.dumps(kv, ensure_ascii=False).encode()
    z = zone_key.encode()
    return struct.pack('<H', len(z)) + z + struct.pack('<I', len(body)) + body

def dec_world_state(p):
    (n,) = struct.unpack('<H', p[:2]); zone = p[2:2+n].decode()
    (m,) = struct.unpack('<I', p[2+n:6+n]); kv = json.loads(p[6+n:6+n+m])
    return zone, kv

# --- JWT (HS256) — authserver подписывает, worldserver валидирует; prod: EdDSA/RSA ---
def jwt_sign(payload: dict, secret: bytes) -> str:
    import base64
    def b64(d): return base64.urlsafe_b64encode(d).rstrip(b'=')
    h = b64(json.dumps({'alg':'HS256','typ':'JWT'}).encode())
    c = b64(json.dumps(payload).encode())
    sig = b64(hashlib.sha256(h + b'.' + c + b'.' + secret).digest())
    return (h + b'.' + c + b'.' + sig).decode()

def jwt_verify(token: str, secret: bytes) -> dict:
    import base64, hmac
    def b64d(s): 
        s += '=' * (-len(s) % 4); return base64.urlsafe_b64decode(s)
    h, c, sig = token.split('.')
    expect = base64.urlsafe_b64encode(hashlib.sha256((h+'.'+c).encode() + b'.' + secret).digest()).rstrip(b'=').decode()
    if not hmac.compare_digest(sig, expect):
        raise ValueError('jwt signature mismatch')
    claims = json.loads(b64d(c))
    if claims.get('exp', 0) < time.time():
        raise ValueError('jwt expired')
    return claims
