// Copyright «Огни душ Падших Валькирий» project. All Rights Reserved.
// ogni-worldserver (прод, C++) — UDP :7777 wire-протокол.
// Спецификация поведения: Server/tools/worldserver_ref.py (Python-референс + smoke_characters.py acceptance).
// Одиночный recv-поток; сессии в таблице под мьютексом; validate у authserver TCP:3724.
#include "common/ogni_common.h"
#include "common/ogni_db.h"
#include "common/ogni_wire.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <regex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifndef _WIN32
#define closesocket close
#endif

using namespace ogni;

static std::string ENV(const char* k, const std::string& def) { const char* v = getenv(k); return v ? v : def; }

static const int  UDP_PORT   = atoi(ENV("OGNI_WORLD_PORT", "7777").c_str());
static const std::string AUTH_HOST = ENV("OGNI_AUTH_HOST", "127.0.0.1");
static const int  AUTH_TCP   = atoi(ENV("OGNI_AUTH_TCP_PORT", "3724").c_str());
static const int  MAX_PKTS_PER_SEC = 800; // синхронно с ogni-world.conf
static const std::string DB_HOST = ENV("OGNI_DB_HOST", "127.0.0.1");
static const std::string DB_USER = ENV("OGNI_DB_USER", "ogni");
static const std::string DB_PW   = ENV("OGNI_DB_PW", "ogni_dev_pw");
static const int  MAX_CHARS_PER_ACCOUNT = 5;

struct Session
{
	uint32_t seq_out = 0;
	JsonPtr claims;
	std::vector<uint8_t> key;
	int64_t character_id = 0;
};

static std::mutex g_mtx;
static std::map<std::string, Session> g_sessions; // "ip:port" -> session

static std::string DbStr(int64_t v) { return std::to_string(v); }
static std::string DbStr(double v) { char b[32]; snprintf(b, sizeof(b), "%.6f", v); return b; }

// ---- вызов authserver validate (JSON строка + '\n') ----
static JsonPtr AuthValidate(const std::string& jwt)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	timeval tv{3, 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(uint16_t(AUTH_TCP));
	inet_pton(AF_INET, AUTH_HOST.c_str(), &a.sin_addr);
	if (connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(fd); return nullptr; }
	std::string req = "{\"op\":\"validate\",\"jwt\":\"" + jwt + "\"}\n";
	send(fd, req.data(), req.size(), 0);
	std::string line; char c;
	while (recv(fd, &c, 1, 0) == 1) { if (c == '\n') break; line += c; if (line.size() > 65536) break; }
	closesocket(fd);
	return Json::ParseStrict(line);
}

// ---------- персонажная линия ----------
static void SendJson(int sock, const sockaddr_in& to, Session& s, uint16_t msg_id, const JsonPtr& payload)
{
	std::string body = payload->Dump();
	auto f = PackFrame(msg_id, s.seq_out++, reinterpret_cast<const uint8_t*>(body.data()), body.size());
	sendto(sock, f.data(), f.size(), 0, (const sockaddr*)&to, sizeof(to));
}

static JsonPtr TemplatesSummary()
{
	JsonPtr arr = Json::MakeArr();
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_characters");
		auto rows = db.Query("SELECT template_key, race_key, class_key, display_name, description, starting_zone "
		                     "FROM char_template WHERE active=1");
		for (auto& r : rows) arr->Push(Db::ToJson(r, {"template_key","race_key","class_key","display_name","description","starting_zone"}));
	}
	catch (const std::exception& e) { fprintf(stderr, "[world][db] templates: %s\n", e.what()); }
	return arr;
}

static JsonPtr TemplatesFull()
{
	JsonPtr arr = Json::MakeArr();
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_characters");
		auto rows = db.Query("SELECT template_id, template_key, race_key, class_key, display_name, description, starting_zone,"
		                     " appearance_schema, base_hp, resource_type, base_resource, base_stats, active FROM char_template WHERE active=1");
		for (auto& r : rows)
			arr->Push(Db::ToJson(r, {"template_id","template_key","race_key","class_key","display_name","description","starting_zone",
			                         "appearance_schema","base_hp","resource_type","base_resource","base_stats","active"}));
	}
	catch (const std::exception& e) { fprintf(stderr, "[world][db] templates_full: %s\n", e.what()); }
	return arr;
}

// зеркало validate_name() из worldserver_ref.py
static std::string ValidateName(const std::string& name)
{
	static const std::regex re("^[A-Z][a-zA-Z]{1,23}$");
	if (!std::regex_match(name, re)) return "CHAR_BAD_NAME";
	std::string lower; for (char c : name) lower += char(tolower((unsigned char)c));
	try
	{
		Db db(DB_HOST, DB_USER, DB_PW, "ogni_characters");
		auto rows = db.Query("SELECT forbidden_words FROM name_policy LIMIT 1");
		if (!rows.empty())
		{
			JsonPtr fw = Json::ParseStrict(rows[0].vals[0]);
			if (fw && fw->type == Json::Type::Array)
				for (auto& w : fw->arr) if (w->AsString() == lower) return "CHAR_FORBIDDEN_NAME";
		}
	}
	catch (...) {}
	return {};
}

static void HandleCharMessage(int sock, const sockaddr_in& addr, Session& s, uint16_t msg_id, const std::vector<uint8_t>& body)
{
	const int64_t acc = JGetI(s.claims, "sub");
	std::string jti = JGetS(s.claims, "jti");
	std::string body_str(body.begin(), body.end());

	if (msg_id == CHAR_LIST_REQ)
	{
		JsonPtr resp = Json::MakeObj();
		JsonPtr roster = Json::MakeArr();
		try
		{
			Db db(DB_HOST, DB_USER, DB_PW, "ogni_auth");
			auto rows = db.Query("SELECT id,name,race_key,class_key,level FROM characters WHERE account_id=? AND deleted_at IS NULL", {DbStr(acc)});
			for (auto& r : rows) roster->Push(Db::ToJson(r, {"id","name","race_key","class_key","level"}));
		}
		catch (const std::exception& e) { fprintf(stderr, "[world][db] roster: %s\n", e.what()); }
		resp->Set("characters", roster);
		resp->Set("templates", TemplatesSummary());
		resp->SetN("max_chars", MAX_CHARS_PER_ACCOUNT);
		SendJson(sock, addr, s, CHAR_LIST, resp);
		return;
	}
	if (msg_id == CHAR_TEMPLATES) // C2S-триггер полного schema (тот же msg_id, что и S2C — как в референсе)
	{
		JsonPtr resp = Json::MakeObj();
		resp->Set("templates", TemplatesFull());
		SendJson(sock, addr, s, CHAR_TEMPLATES, resp);
		return;
	}
	if (msg_id == CHAR_CREATE)
	{
		JsonPtr errr = Json::MakeObj();
		JsonPtr req = Json::ParseStrict(body_str);
		if (!req) { errr->SetS("error", "CHAR_BAD_JSON"); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }
		std::string tkey = JGetS(req, "template_key"), name = JGetS(req, "name");
		const JsonPtr* appv = req->Find("appearance");
		JsonPtr appearance = appv ? *appv : Json::MakeObj();

		std::string nerr = ValidateName(name);
		if (!nerr.empty()) { errr->SetS("error", nerr); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }

		try
		{
			Db dc(DB_HOST, DB_USER, DB_PW, "ogni_characters");
			auto trows = dc.Query("SELECT template_id, race_key, class_key, appearance_schema, base_hp, resource_type, base_resource, base_stats, starting_zone "
			                      "FROM char_template WHERE template_key=? AND active=1", {tkey});
			if (trows.empty()) { errr->SetS("error", "CHAR_UNKNOWN_TEMPLATE"); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }
			auto& tpl = trows[0];
			int64_t template_id = atoll(tpl.vals[0].c_str());
			std::string schema_json = tpl.vals[3];

			// серверная валидация внешности по allowed_values
			JsonPtr schema = Json::ParseStrict(schema_json);
			if (schema && schema->type == Json::Type::Object)
			{
				const JsonPtr* slots_p = schema->Find("slots");
				JsonPtr slots = slots_p ? *slots_p : nullptr;
				if (slots)
				{
					for (auto& kv : appearance->obj)
					{
						const JsonPtr* allowed_p = slots->Find(kv.first);
						if (!allowed_p || (*allowed_p)->type != Json::Type::Array)
						{ errr->SetS("error", "CHAR_BAD_APPEARANCE:" + kv.first); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }
						bool found = false;
						for (auto& cand : (*allowed_p)->arr) if (cand->AsString() == kv.second->AsString()) { found = true; break; }
						if (!found)
						{ errr->SetS("error", "CHAR_BAD_APPEARANCE:" + kv.first); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }
					}
				}
			}

			Db da(DB_HOST, DB_USER, DB_PW, "ogni_auth");
			auto cnt = da.Query("SELECT COUNT(*) n FROM characters WHERE account_id=? AND deleted_at IS NULL", {DbStr(acc)});
			if (!cnt.empty() && atoll(cnt[0].vals[0].c_str()) >= MAX_CHARS_PER_ACCOUNT)
			{ errr->SetS("error", "CHAR_LIMIT"); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }

			uint64_t cid;
			try
			{
				cid = da.InsertReturnId("INSERT INTO characters (account_id,name,race_key,class_key,level) VALUES (?,?,?,?,1)",
					{DbStr(int64_t(acc)), name, tpl.vals[1], tpl.vals[2]});
			}
			catch (const std::exception&)
			{ errr->SetS("error", "CHAR_NAME_TAKEN"); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); return; }

			dc.Execute("INSERT INTO character_appearance (character_id, template_id, appearance_json) VALUES (?,?,?)",
				{DbStr(int64_t(cid)), DbStr(template_id), appearance->Dump()});
			dc.Execute("INSERT INTO character_snapshot (character_id, base_hp, resource_type, base_resource, stats_json, starting_zone) VALUES (?,?,?,?,?,?)",
				{DbStr(int64_t(cid)), tpl.vals[4], tpl.vals[5], tpl.vals[6], tpl.vals[7], tpl.vals[8]});

			JsonPtr ok = Json::MakeObj();
			ok->SetN("character_id", double(cid));
			ok->SetS("name", name);
			ok->SetS("template_key", tkey);
			SendJson(sock, addr, s, CHAR_CREATE_OK, ok);
			printf("[world] char created acc=%lld id=%llu name=%s tpl=%s\n", (long long)acc, (unsigned long long)cid, name.c_str(), tkey.c_str()); fflush(stdout);
		}
		catch (const std::exception& e)
		{ fprintf(stderr, "[world][db] create: %s\n", e.what()); errr->SetS("error", "CHAR_INTERNAL"); SendJson(sock, addr, s, CHAR_CREATE_OK, errr); }
		return;
	}
	if (msg_id == CHAR_ENTER_WORLD)
	{
		JsonPtr errr = Json::MakeObj();
		JsonPtr req = Json::ParseStrict(body_str);
		if (!req) { errr->SetS("error", "CHAR_BAD_JSON"); SendJson(sock, addr, s, WORLD_READY, errr); return; }
		int64_t cid = JGetI(req, "character_id");

		Db da(DB_HOST, DB_USER, DB_PW, "ogni_auth");
		auto ch_rows = da.Query("SELECT id,name FROM characters WHERE id=? AND account_id=? AND deleted_at IS NULL", {DbStr(cid), DbStr(acc)});
		if (ch_rows.empty()) { errr->SetS("error", "CHAR_NOT_OWNED"); SendJson(sock, addr, s, WORLD_READY, errr); return; }
		std::string char_name = ch_rows[0].vals[1];

		Db dc(DB_HOST, DB_USER, DB_PW, "ogni_characters");
		auto snap_rows = dc.Query("SELECT base_hp, resource_type, base_resource, stats_json, starting_zone FROM character_snapshot WHERE character_id=?", {DbStr(cid)});
		if (snap_rows.empty()) { errr->SetS("error", "CHAR_NO_SNAPSHOT"); SendJson(sock, addr, s, WORLD_READY, errr); return; }
		auto& snap = snap_rows[0];
		std::string zone = snap.vals[4];
		double hp = atof(snap.vals[0].c_str());
		double pos[3] = {10.0, 20.0, 0.0};

		Db dw(DB_HOST, DB_USER, DB_PW, "ogni_world");
		dw.Execute("REPLACE INTO character_state (character_id, zone_key, pos_x, pos_y, pos_z, hp, max_hp, resource_type, resource_value, level) "
		           "VALUES (?,?,?,?,?,?,?,?,?,1)",
			{DbStr(cid), zone, DbStr(pos[0]), DbStr(pos[1]), DbStr(pos[2]), snap.vals[0], snap.vals[0], snap.vals[1], snap.vals[2]});
		dw.Execute("REPLACE INTO active_session (session_id, character_id, world_server_id, udp_endpoint) VALUES (?, ?, 1, ?)",
			{jti, DbStr(cid), [&]{ char b[64]; inet_ntop(AF_INET, &addr.sin_addr, b, sizeof(b)); std::string ep = std::string(b) + ":" + std::to_string(ntohs(addr.sin_port)); return ep; }()});
		s.character_id = cid;

		JsonPtr wr = Json::MakeObj();
		wr->SetN("character_id", double(cid));
		wr->SetS("name", char_name);
		wr->SetS("zone_key", zone);
		JsonPtr pos_arr = Json::MakeArr();
		pos_arr->Push(Json::MakeNum(pos[0])); pos_arr->Push(Json::MakeNum(pos[1])); pos_arr->Push(Json::MakeNum(pos[2]));
		wr->Set("pos", pos_arr);
		wr->SetN("hp", hp);
		wr->SetN("max_hp", hp);
		wr->SetS("resource_type", snap.vals[1]);
		wr->SetN("resource", atof(snap.vals[2].c_str()));
		JsonPtr stats = Json::ParseStrict(snap.vals[3]);
		wr->Set("stats", stats ? stats : Json::MakeObj());
		SendJson(sock, addr, s, WORLD_READY, wr);
		printf("[world] enter-world acc=%lld char=%lld zone=%s\n", (long long)acc, (long long)cid, zone.c_str()); fflush(stdout);
		return;
	}
}

static void HandleFrame(int sock, const uint8_t* data, size_t len, const sockaddr_in& addr)
{
	Frame fr;
	if (!UnpackFrame(data, len, fr)) return; // мусор/спуфинг — дроп без ответа
	char ipbuf[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
	std::string ekey = std::string(ipbuf) + ":" + std::to_string(ntohs(addr.sin_port));

	std::unique_lock<std::mutex> lk(g_mtx);
	auto it = g_sessions.find(ekey);

	if (fr.msg_id == SESSION_HELLO)
	{
		if (fr.body.size() < 2) return;
		uint16_t jwt_len = get_le16(fr.body.data());
		if (fr.body.size() < size_t(2 + jwt_len)) return;
		std::string jwt(fr.body.begin() + 2, fr.body.begin() + 2 + jwt_len);
		lk.unlock(); // сетевой вызов к authserver — безheld lock
		JsonPtr res = AuthValidate(jwt);
		lk.lock();
		bool ok = false;
		JsonPtr claims;
		if (res)
		{
			if (const JsonPtr* o = res->Find("ok")) ok = (*o)->type == Json::Type::Bool && (*o)->b;
			if (const JsonPtr* c = res->Find("claims")) claims = *c;
		}
		if (!ok || !claims)
		{
			uint8_t one = 1;
			auto f = PackFrame(PROTOCOL_MISMATCH, 0, &one, 1);
			sendto(sock, f.data(), f.size(), 0, (const sockaddr*)&addr, sizeof(addr));
			return;
		}
		Session s;
		s.claims = claims;
		s.key = RandomBytes(32);
		g_sessions[ekey] = s;
		auto body = EncodeSessionAcceptWire(s.key, 0xA05EED);
		auto f = PackFrame(SESSION_ACCEPT, s.seq_out++, body.data(), body.size());
		sendto(sock, f.data(), f.size(), 0, (const sockaddr*)&addr, sizeof(addr));
		printf("[world] session open acc=%lld from %s\n", (long long)JGetI(s.claims, "sub"), ekey.c_str()); fflush(stdout);
		return;
	}
	if (it == g_sessions.end()) return; // без сессии ничего не принимаем
	Session& s = it->second;

	switch (fr.msg_id)
	{
	case MOVE_UPDATE:
	{
		MoveUpdateBody mv;
		if (DecodeMoveUpdate(fr.body.data(), fr.body.size(), mv)) {} // серверный авторитет: collision/anti-cheat — в map-модуле
		break;
	}
	case SWING_BEGIN:
	{
		if (fr.body.size() < 32) break;
		uint32_t weapon_id; std::memcpy(&weapon_id, fr.body.data(), 4);
		// упрощённый server-side hit-check (как в референсе): victim guid=0x42
		std::vector<uint8_t> result(8 + 8 + 1 + 4 + 4);
		uint64_t sub = uint64_t(JGetI(s.claims, "sub"));
		uint64_t victim = 0x42; uint8_t outcome = 1; uint32_t dmg = 10 + (weapon_id % 20); uint32_t soul = 3;
		memcpy(result.data(), &sub, 8); memcpy(result.data()+8, &victim, 8); result[16] = outcome;
		memcpy(result.data()+17, &dmg, 4); memcpy(result.data()+21, &soul, 4);
		auto f = PackFrame(SWING_RESULT, s.seq_out++, result.data(), result.size());
		sendto(sock, f.data(), f.size(), 0, (const sockaddr*)&addr, sizeof(addr));
		break;
	}
	case CLAN_PING:
	{
		JsonPtr o = Json::MakeObj(); o->SetS("clan", "hraesvelgr_clan"); o->SetN("score", 0);
		SendJson(sock, addr, s, REP_CHANGE, o);
		break;
	}
	case CHAR_LIST_REQ: case CHAR_TEMPLATES: case CHAR_CREATE: case CHAR_ENTER_WORLD:
		HandleCharMessage(sock, addr, s, fr.msg_id, fr.body);
		break;
	default: break;
	}
}

int main()
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(uint16_t(UDP_PORT));
	if (bind(sock, (sockaddr*)&a, sizeof(a)) != 0) { perror("[world] bind udp"); return 1; }
	printf("[world] UDP :%d ready (prod C++)\n", UDP_PORT); fflush(stdout);
	std::vector<uint8_t> buf(2048);
	for (;;)
	{
		sockaddr_in from{}; socklen_t fl = sizeof(from);
		ssize_t n = recvfrom(sock, buf.data(), buf.size(), 0, (sockaddr*)&from, &fl);
		if (n <= 0) continue;
		HandleFrame(sock, buf.data(), size_t(n), from);
	}
	return 0;
}
