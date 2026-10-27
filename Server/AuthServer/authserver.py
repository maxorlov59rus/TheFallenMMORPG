#!/usr/bin/env python3
"""
ogni-authserver — сервис авторизации «Огни душ Падших Валькирий».
Прототип связки (Python stdlib + mysql-connector): в проде переносится на C++/Win64
с той же контрактной поверхностью (этот файл = спецификация поведения).

Каналы:
  HTTPS/JSON :4443  POST /api/v1/login   {username,password,client_build} -> {token, world_host, world_port}
  TCP        :3724  internal validate    {"op":"validate","jwt":...}       -> {"ok":true,"claims":...} | revoke
JWT: HS256 dev-secret (ОБЯЗАТЕЛЬНО задать OGNI_JWT_SECRET извне — см. main()); prod: Ed25519 + JWKS.
Пароли: Argon2id (m=64MiB,t=3,p=4); легаси-PBKDF2 хэши прозрачно мигрируются при успешном логине.
"""
import json, os, sys, hashlib, secrets, time, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from socketserver import ThreadingTCPServer, BaseRequestHandler

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
from ogni_wire import jwt_sign, jwt_verify

try:
    from argon2.low_level import hash_secret as _a2_hash, verify_secret as _a2_verify, Type as _a2Type
    HAVE_ARGON2 = True
except ImportError:
    HAVE_ARGON2 = False

DB_HOST = os.environ.get('OGNI_DB_HOST', '127.0.0.1')
DB_USER = os.environ.get('OGNI_DB_USER', 'ogni')
DB_PW   = os.environ.get('OGNI_DB_PW', 'ogni_dev_pw')  # dev-стенд; прод: OGNI_DB_PW из secret-store
JWT_SECRET = os.environ.get('OGNI_JWT_SECRET', 'dev-only-insecure-secret').encode()
TOKEN_TTL = int(os.environ.get('OGNI_TOKEN_TTL', '300'))
WORLD_HOST = os.environ.get('OGNI_WORLD_HOST', '127.0.0.1')
WORLD_PORT = int(os.environ.get('OGNI_WORLD_PORT', '7777'))
MAX_FAILED = 5
LOCK_MIN = 15

def db():
    import mysql.connector
    return mysql.connector.connect(host=DB_HOST, user=DB_USER, password=DB_PW, database='ogni_auth')

A2_PARAMS = dict(time_cost=3, memory_cost=65536, parallelism=4, hash_len=32, type=_a2Type.ID) if HAVE_ARGON2 else None

def hash_pw(pw: str) -> str:
    """Argon2id — основной формат ('$argon2id$...'). Fallback без либы: PBKDF2 (только dev)."""
    if HAVE_ARGON2:
        salt = secrets.token_bytes(16)
        return _a2_hash(pw.encode(), salt, **A2_PARAMS).decode()
    salt = secrets.token_hex(16)
    dk = hashlib.pbkdf2_hmac('sha256', pw.encode(), bytes.fromhex(salt), 200_000).hex()
    return f'pbkdf2$200000${salt}${dk}'

def check_pw(pw: str, stored: str) -> bool:
    """Возвращает True/False; special-case 'migrate' = пароль верный, но хэш легаси — надо переписать."""
    if stored.startswith('$argon2'):
        if not HAVE_ARGON2:
            print('[auth] FATAL: argon2-хэш в БД, а argon2-cffi не установлен', flush=True)
            return False
        try:
            return _a2_verify(stored.encode(), pw.encode(), type=_a2Type.ID)
        except Exception:
            return False
    # легаси pbkdf2$iters$salt$dk
    try:
        _, iters, salt, dk = stored.split('$')
        cand = hashlib.pbkdf2_hmac('sha256', pw.encode(), bytes.fromhex(salt), int(iters)).hex()
        if secrets.compare_digest(cand, dk):
            return 'migrate' if HAVE_ARGON2 else True
        return False
    except Exception:
        return False

class AuditMixin:
    def audit(self, acc_id, ip, result):
        try:
            c = db(); cur = c.cursor()
            cur.execute("INSERT INTO login_audit (account_id, ip, result) VALUES (%s, INET6_ATON(%s), %s)", (acc_id, ip, result))
            c.commit(); c.close()
        except Exception as e:
            print('[audit-fail]', e)

class AuthAPI(BaseHTTPRequestHandler, AuditMixin):
    def log_message(self, *a): pass
    def _json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)

    def do_POST(self):
        if self.path != '/api/v1/login':
            return self._json(404, {'error': 'not_found'})
        n = int(self.headers.get('Content-Length', 0))
        if n > 4096: return self._json(413, {'error': 'too_big'})
        try:
            req = json.loads(self.rfile.read(n))
            user, pw = str(req['username'])[:32], str(req['password'])[:128]
            build = str(req.get('client_build', ''))[:32]
        except Exception:
            return self._json(400, {'error': 'bad_json'})
        ip = self.client_address[0]
        c = db(); cur = c.cursor(dictionary=True)
        cur.execute("SELECT * FROM account WHERE username=%s", (user,))
        acc = cur.fetchone()
        if not acc:
            self.audit(None, ip, 'unknown_user'); c.close()
            return self._json(401, {'error': 'AUTH_BAD_CREDENTIALS'})  # не различаем user/pass — anti-enumeration
        if acc['locked_until'] and acc['locked_until'].timestamp() > time.time():
            self.audit(acc['id'], ip, 'locked'); c.close()
            return self._json(423, {'error': 'AUTH_LOCKED'})
        pw_ok = check_pw(pw, acc['pass_hash'])
        if pw_ok is False or pw_ok == 0:
            cur.execute("UPDATE account SET failed_logins=failed_logins+1, locked_until=IF(failed_logins+1>=%s, NOW()+INTERVAL %s MINUTE, locked_until) WHERE id=%s",
                        (MAX_FAILED, LOCK_MIN, acc['id'])); c.commit()
            self.audit(acc['id'], ip, 'bad_password'); c.close()
            return self._json(401, {'error': 'AUTH_BAD_CREDENTIALS'})
        if pw_ok == 'migrate':
            # прозрачная миграция легаси-PBKDF2 -> Argon2id при успешном входе
            cur.execute("UPDATE account SET pass_hash=%s WHERE id=%s", (hash_pw(pw), acc['id']))
        if acc['status'] != 'active':
            self.audit(acc['id'], ip, 'locked'); c.close()
            return self._json(403, {'error': f"AUTH_{acc['status'].upper()}"})
        cur.execute("SELECT COUNT(*) AS n FROM allowed_builds WHERE build_tag=%s AND active=1", (build,))
        if not cur.fetchone()['n']:
            self.audit(acc['id'], ip, 'build_mismatch'); c.close()
            return self._json(426, {'error': 'BUILD_MISMATCH', 'required_build': 'see launcher'})
        # OK: выдаём JWT + session_key; резервируем строку под world handshake
        jti = secrets.uuid() if hasattr(secrets,'uuid') else __import__('uuid').uuid4().hex
        skey = secrets.token_bytes(32)
        claims = {'sub': acc['id'], 'jti': jti, 'exp': int(time.time()) + TOKEN_TTL, 'build': build}
        token = jwt_sign(claims, JWT_SECRET)
        cur.execute("UPDATE account SET failed_logins=0, last_login_at=NOW() WHERE id=%s", (acc['id'],))
        cur.execute("INSERT INTO auth_session (account_id, jwt_id, session_key, ip, client_build, expires_at) "
                    "VALUES (%s,%s,%s,INET6_ATON(%s),%s, NOW()+INTERVAL %s SECOND)",
                    (acc['id'], jti, skey, ip, build, TOKEN_TTL))
        c.commit(); c.close()
        self.audit(acc['id'], ip, 'ok')
        return self._json(200, {'token': token, 'world_host': WORLD_HOST, 'world_port': WORLD_PORT,
                                'session_key_b64': __import__('base64').b64encode(skey).decode(),
                                'ttl_sec': TOKEN_TTL})

class InternalValidator(threading.Thread):
    """TCP :3724 — worldserver проверяет/отзывает токен. Строка JSON + '\n'."""
    def __init__(self, conn):
        super().__init__(daemon=True); self.conn = conn
    def run(self):
        try:
            f = self.conn.makefile('rwb')
            line = f.readline(65536)
            if not line: return
            req = json.loads(line)
            op = req.get('op'); resp = {}
            if op == 'validate':
                try:
                    claims = jwt_verify(req['jwt'], JWT_SECRET)
                    c = db(); cur = c.cursor(dictionary=True)
                    cur.execute("SELECT revoked_at, expires_at FROM auth_session WHERE jwt_id=%s", (claims['jti'],))
                    row = cur.fetchone(); c.close()
                    if not row or row['revoked_at'] or row['expires_at'].timestamp() < time.time():
                        resp = {'ok': False, 'error': 'revoked_or_expired'}
                    else:
                        resp = {'ok': True, 'claims': claims}
                except ValueError as e:
                    resp = {'ok': False, 'error': str(e)}
            elif op == 'revoke':
                c = db(); cur = c.cursor()
                cur.execute("UPDATE auth_session SET revoked_at=NOW() WHERE jwt_id=%s", (req['jti'],)); c.commit(); c.close()
                resp = {'ok': True}
            else:
                resp = {'ok': False, 'error': 'unknown_op'}
            f.write((json.dumps(resp) + '\n').encode()); f.flush()
        except Exception as e:
            import traceback; traceback.print_exc()
        finally:
            try: self.conn.close()
            except Exception: pass

class ValidatorServer(ThreadingTCPServer):
    allow_reuse_address = True
    def get_request(self):
        conn, addr = super().get_request()
        return conn, addr
    def process_request(self, request, client_address):
        InternalValidator(request).start()

def main():
    # SECURITY: dev-секрет запрещён вне явного стендового флага
    if JWT_SECRET == b'dev-only-insecure-secret' and os.environ.get('OGNI_ALLOW_DEV_SECRETS') != '1':
        sys.exit('[auth] FATAL: OGNI_JWT_SECRET не задан. Для локального стенда — OGNI_ALLOW_DEV_SECRETS=1.')
    hs_port = int(os.environ.get('OGNI_AUTH_HTTP_PORT', '4443'))
    tcp_port = int(os.environ.get('OGNI_AUTH_TCP_PORT', '3724'))
    srv_tcp = ValidatorServer(('127.0.0.1', tcp_port), None)
    threading.Thread(target=srv_tcp.serve_forever, daemon=True).start()
    httpd = ThreadingHTTPServer(('0.0.0.0', hs_port), AuthAPI)
    httpd.daemon_threads = True
    print(f'[auth] HTTP(dev-plain) :{hs_port} /api/v1/login ; internal TCP :{tcp_port}', flush=True)
    httpd.serve_forever()

if __name__ == '__main__':
    main()
