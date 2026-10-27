// Copyright «Огни душ Падших Валькирий» project. All Rights Reserved.
// ogni-authserver (прод, C++) — спецификация поведения: Server/AuthServer/authserver.py (Python-референс).
// Каналы: HTTP :4443 POST /api/v1/login ; TCP 127.0.0.1:3724 internal validate/revoke (JSON строка + '\n').
// Win64-цель: MSVC v143 /arch:AVX2 RelWithDebInfo; POSIX-сборка используется для CI/smoke на стенде.
#include "common/ogni_common.h"
#include "common/ogni_db.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#ifndef _WIN32
#define closesocket close
#endif

using namespace ogni;

static std::string ENV(const char* k, const std::string& def) { const char* v = getenv(k); return v ? v : def; }

static const std::string DB_HOST   = ENV("OGNI_DB_HOST", "127.0.0.1");
static const std::string DB_USER   = ENV("OGNI_DB_USER", "ogni");
static const std::string DB_PW     = ENV("OGNI_DB_PW", "ogni_dev_pw");
static const std::string JWT_SECRET = ENV("OGNI_JWT_SECRET", "dev-only-insecure-secret");
static const int  TOKEN_TTL  = atoi(ENV("OGNI_TOKEN_TTL", "300").c_str());
static const std::string WORLD_HOST = ENV("OGNI_WORLD_HOST", "127.0.0.1");
static const int  WORLD_PORT = atoi(ENV("OGNI_WORLD_PORT", "7777").c_str());
static const int  MAX_FAILED = 5;
static const int  LOCK_MIN   = 15;
static const int  HTTP_PORT  = atoi(ENV("OGNI_AUTH_HTTP_PORT", "4443").c_str());
static const int  TCP_PORT   = atoi(ENV("OGNI_AUTH_TCP_PORT", "3724").c_str());

static void Audit(const std::string& account_id_or_empty, const std::string& ip, const std::string& result)
{
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_auth");
		if (account_id_or_empty.empty())
			db.Execute("INSERT INTO login_audit (account_id, ip, result) VALUES (NULL, INET6_ATON(?), ?)", {ip, result});
		else
			db.Execute("INSERT INTO login_audit (account_id, ip, result) VALUES (?, INET6_ATON(?), ?)", {account_id_or_empty, ip, result});
	}
	catch (const std::exception& e) { fprintf(stderr, "[audit-fail] %s\n", e.what()); }
}

struct LoginResult { int code; std::string body; };

static LoginResult HandleLogin(const std::string& user, const std::string& pw, const std::string& build, const std::string& ip)
{
	auto err = [](int code, const std::string& what) -> LoginResult {
		JsonPtr o = Json::MakeObj(); o->SetS("error", what); return {code, o->Dump()};
	};
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_auth");
		auto rows = db.Query("SELECT id, pass_hash, status FROM account WHERE username=?", {user});
		if (rows.empty()) { Audit("", ip, "unknown_user"); return err(401, "AUTH_BAD_CREDENTIALS"); } // anti-enumeration
		const auto& acc = rows[0];
		std::string acc_id = acc.vals[0];

		auto lk = db.Query("SELECT locked_until > NOW() AS is_locked FROM account WHERE id=?", {acc_id});
		if (!lk.empty() && !lk[0].nulls[0] && lk[0].vals[0] == "1")
		{ Audit(acc_id, ip, "locked"); return err(423, "AUTH_LOCKED"); }

		PwResult pr = CheckPassword(pw, acc.vals[1]);
		if (pr.kind == PwResult::Bad)
		{
			db.Execute("UPDATE account SET failed_logins=failed_logins+1, "
			           "locked_until=IF(failed_logins+1>=?, NOW()+INTERVAL ? MINUTE, locked_until) WHERE id=?",
			           {std::to_string(MAX_FAILED), std::to_string(LOCK_MIN), acc_id});
			Audit(acc_id, ip, "bad_password");
			return err(401, "AUTH_BAD_CREDENTIALS");
		}
		if (pr.kind == PwResult::Migrate) // прозрачная миграция PBKDF2 -> Argon2id при успешном входе
			db.Execute("UPDATE account SET pass_hash=? WHERE id=?", {HashArgon2id(pw), acc_id});

		if (acc.vals[2] != "active") { Audit(acc_id, ip, "locked"); return err(403, "AUTH_" + acc.vals[2]); }

		auto ab = db.Query("SELECT COUNT(*) AS n FROM allowed_builds WHERE build_tag=? AND active=1", {build});
		if (ab.empty() || ab[0].vals[0] == "0") { Audit(acc_id, ip, "build_mismatch"); return err(426, "BUILD_MISMATCH"); }

		std::string jti = UuidV4();
		std::vector<uint8_t> skey = RandomBytes(32);
		JsonPtr claims = Json::MakeObj();
		claims->SetN("sub", double(atoll(acc_id.c_str())));
		claims->SetS("jti", jti);
		claims->SetN("exp", double(time(nullptr) + TOKEN_TTL));
		claims->SetS("build", build);
		std::string token = JwtSign(claims, JWT_SECRET);

		db.Execute("UPDATE account SET failed_logins=0, last_login_at=NOW() WHERE id=?", {acc_id});
		db.Execute("INSERT INTO auth_session (account_id, jwt_id, session_key, ip, client_build, expires_at) "
		           "VALUES (?, ?, ?, INET6_ATON(?), ?, NOW()+INTERVAL ? SECOND)",
		           {acc_id, jti, std::string(skey.begin(), skey.end()), ip, build, std::to_string(TOKEN_TTL)});

		Audit(acc_id, ip, "ok");
		JsonPtr resp = Json::MakeObj();
		resp->SetS("token", token);
		resp->SetS("world_host", WORLD_HOST);
		resp->SetN("world_port", WORLD_PORT);
		resp->SetS("session_key_b64", B64UrlEncode(skey.data(), skey.size()));
		resp->SetN("ttl_sec", TOKEN_TTL);
		return {200, resp->Dump()};
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "[auth][db-error] %s\n", e.what());
		return err(500, "AUTH_INTERNAL");
	}
}

// ---------- простейший HTTP (только POST /api/v1/login; dev-plain как в Python-стенде) ----------
static void ServeHttpConn(int fd, std::string peer_ip)
{
	char buf[8192]; size_t total = 0;
	std::string req;
	while (total < sizeof(buf) - 1)
	{
		ssize_t n = recv(fd, buf + total, sizeof(buf) - 1 - total, 0);
		if (n <= 0) break;
		total += n; buf[total] = 0;
		req.assign(buf, total);
		if (req.find("\r\n\r\n") != std::string::npos) break;
	}
	if (req.compare(0, 4, "POST") != 0 || req.find("/api/v1/login") == std::string::npos)
	{
		const char* r404 = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"not_found\"}";
		send(fd, r404, strlen(r404), 0); closesocket(fd); return;
	}
	size_t hend = req.find("\r\n\r\n");
	size_t clen = 0;
	{
		std::string headers = req.substr(0, hend);
		std::transform(headers.begin(), headers.end(), headers.begin(), ::tolower);
		size_t p = headers.find("content-length:");
		if (p != std::string::npos) clen = strtoul(headers.c_str() + p + 15, nullptr, 10);
	}
	while (clen > 0 && hend + 4 + clen > req.size())
	{
		ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
		if (n <= 0) break;
		buf[n] = 0; req.append(buf, n);
	}
	int code = 400; std::string out;
	if (clen > 4096) { code = 413; out = "{\"error\":\"too_big\"}"; }
	else
	{
		JsonPtr j = Json::ParseStrict(req.substr(hend + 4, clen));
		if (j)
		{
			std::string user = JGetS(j, "username"); if (user.size() > 32) user = user.substr(0, 32);
			std::string pw = JGetS(j, "password"); if (pw.size() > 128) pw = pw.substr(0, 128);
			std::string build = JGetS(j, "client_build"); if (build.size() > 32) build = build.substr(0, 32);
			LoginResult lr = HandleLogin(user, pw, build, peer_ip);
			code = lr.code; out = lr.body;
		}
		else out = "{\"error\":\"bad_json\"}";
	}

	const char* reason = code == 200 ? "OK" : code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" :
		code == 413 ? "Payload Too Large" : code == 423 ? "Locked" : code == 426 ? "Upgrade Required" :
		code == 500 ? "Internal Server Error" : "Bad Request";
	char head[256];
	int n = snprintf(head, sizeof(head),
		"HTTP/1.1 %d %s\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
		code, reason, out.size());
	send(fd, head, n, 0);
	send(fd, out.data(), out.size(), 0);
	closesocket(fd);
}

static void HttpAcceptLoop()
{
	int ls = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(uint16_t(HTTP_PORT));
	if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0) { perror("[auth] bind http"); exit(1); }
	listen(ls, 64);
	printf("[auth] HTTP(dev-plain) :%d /api/v1/login\n", HTTP_PORT); fflush(stdout);
	for (;;)
	{
		sockaddr_in ca{}; socklen_t cl = sizeof(ca);
		int fd = accept(ls, (sockaddr*)&ca, &cl);
		if (fd < 0) continue;
		char ipstr[INET_ADDRSTRLEN] = "127.0.0.1";
		inet_ntop(AF_INET, &ca.sin_addr, ipstr, sizeof(ipstr));
		std::thread(ServeHttpConn, fd, std::string(ipstr)).detach();
	}
}

// ---------- internal validator TCP:3724 ----------
static std::string ValidateJwt(const std::string& token)
{
	JsonPtr claims;
	if (!JwtVerify(token, JWT_SECRET, claims))
	{ JsonPtr o = Json::MakeObj(); o->SetB("ok", false); o->SetS("error", "signature_or_expired"); return o->Dump(); }
	std::string jti = JGetS(claims, "jti");
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_auth");
		auto rows = db.Query("SELECT revoked_at IS NOT NULL AS rev, expires_at > NOW() AS live FROM auth_session WHERE jwt_id=?", {jti});
		JsonPtr o = Json::MakeObj();
		if (rows.empty() || rows[0].vals[0] == "1" || rows[0].vals[1] == "0")
		{ o->SetB("ok", false); o->SetS("error", "revoked_or_expired"); }
		else { o->SetB("ok", true); o->Set("claims", claims); }
		return o->Dump();
	}
	catch (const std::exception& e)
	{ JsonPtr o = Json::MakeObj(); o->SetB("ok", false); o->SetS("error", e.what()); return o->Dump(); }
}

static std::string RevokeSession(const std::string& jti)
{
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_auth");
		db.Execute("UPDATE auth_session SET revoked_at=NOW() WHERE jwt_id=?", {jti});
		JsonPtr o = Json::MakeObj(); o->SetB("ok", true); return o->Dump();
	}
	catch (const std::exception& e)
	{ JsonPtr o = Json::MakeObj(); o->SetB("ok", false); o->SetS("error", e.what()); return o->Dump(); }
}

static void ServeTcpConn(int fd)
{
	std::string line; char c;
	while (recv(fd, &c, 1, 0) == 1)
	{
		if (c == '\n') break;
		line += c;
		if (line.size() > 65536) break;
	}
	if (line.empty()) { closesocket(fd); return; }
	JsonPtr req = Json::ParseStrict(line);
	std::string resp;
	if (!req) resp = "{\"ok\":false,\"error\":\"bad_json\"}";
	else
	{
		std::string op = JGetS(req, "op");
		if (op == "validate") resp = ValidateJwt(JGetS(req, "jwt"));
		else if (op == "revoke") resp = RevokeSession(JGetS(req, "jti"));
		else resp = "{\"ok\":false,\"error\":\"unknown_op\"}";
	}
	resp += "\n";
	send(fd, resp.data(), resp.size(), 0);
	closesocket(fd);
}

static void TcpAcceptLoop()
{
	int ls = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(uint16_t(TCP_PORT));
	if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0) { perror("[auth] bind tcp"); exit(1); }
	listen(ls, 64);
	printf("[auth] internal TCP :%d validate/revoke\n", TCP_PORT); fflush(stdout);
	for (;;)
	{
		int fd = accept(ls, nullptr, nullptr);
		if (fd < 0) continue;
		std::thread(ServeTcpConn, fd).detach();
	}
}

int main()
{
	// SECURITY: dev-секрет запрещён вне явного стендового флага (зеркало authserver.py::main)
	if (JWT_SECRET == "dev-only-insecure-secret" && ENV("OGNI_ALLOW_DEV_SECRETS", "") != "1")
	{ fprintf(stderr, "[auth] FATAL: OGNI_JWT_SECRET не задан. Для локального стенда — OGNI_ALLOW_DEV_SECRETS=1.\n"); return 1; }
	std::thread t1(TcpAcceptLoop);
	std::thread t2(HttpAcceptLoop);
	t1.join(); t2.join();
	return 0;
}
