// Copyright «Огни душ Падших Валькирий» project. All Rights Reserved.
// Общие утилиты прод-серверов: base64, hex, CSPRNG, JWT HS256 (совместим с ogni_wire.py),
// Argon2id (libargon2) + легаси-PBKDF2 через OpenSSL с прозрачной миграцией —
// зеркалит поведение Server/AuthServer/authserver.py (acceptance-спецификации).
#pragma once

#include <argon2.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <sstream>
#include "ogni_json.h"

namespace ogni
{
	inline std::string B64UrlEncode(const uint8_t* p, size_t n)
	{
		static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string out;
		for (size_t i = 0; i < n; i += 3)
		{
			uint32_t v = uint32_t(p[i]) << 16 | (i + 1 < n ? uint32_t(p[i+1]) << 8 : 0) | (i + 2 < n ? uint32_t(p[i+2]) : 0);
			out += T[(v >> 18) & 63];
			out += T[(v >> 12) & 63];
			if (i + 1 < n) out += T[(v >> 6) & 63];
			if (i + 2 < n) out += T[v & 63];
		}
		return out; // без '=' padding — как Python rstrip('=')
	}

	inline std::vector<uint8_t> B64UrlDecode(const std::string& s)
	{
		auto idx = [](char c) -> int {
			if (c >= 'A' && c <= 'Z') return c - 'A';
			if (c >= 'a' && c <= 'z') return c - 'a' + 26;
			if (c >= '0' && c <= '9') return c - '0' + 52;
			if (c == '-' || c == '+') return 62;
			if (c == '_' || c == '/') return 63;
			return -1;
		};
		std::vector<uint8_t> out;
		uint32_t acc = 0; int bits = 0;
		for (char c : s)
		{
			int v = idx(c);
			if (v < 0) continue; // skip padding
			acc = (acc << 6) | uint32_t(v); bits += 6;
			if (bits >= 8) { bits -= 8; out.push_back(uint8_t((acc >> bits) & 0xFF)); }
		}
		return out;
	}

	inline std::string ToHex(const std::string& raw)
	{
		static const char* H = "0123456789abcdef";
		std::string out; out.reserve(raw.size() * 2);
		for (unsigned char c : raw) { out += H[c >> 4]; out += H[c & 15]; }
		return out;
	}

	inline bool FromHex(const std::string& hex, std::string& out)
	{
		if (hex.size() % 2) return false;
		auto val = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		out.clear();
		for (size_t i = 0; i < hex.size(); i += 2)
		{
			int hi = val(hex[i]), lo = val(hex[i+1]);
			if (hi < 0 || lo < 0) return false;
			out += char((hi << 4) | lo);
		}
		return true;
	}

	inline std::vector<uint8_t> RandomBytes(size_t n)
	{
		std::vector<uint8_t> b(n);
		if (RAND_bytes(b.data(), int(n)) != 1)
			for (size_t i = 0; i < n; ++i) b[i] = uint8_t(rand() ^ (uint32_t(time(nullptr)) >> (i % 4))); // не должно случаться
		return b;
	}

	inline std::string UuidV4()
	{
		auto b = RandomBytes(16);
		b[6] = (b[6] & 0x0F) | 0x40; b[8] = (b[8] & 0x3F) | 0x80;
		char out[37];
		snprintf(out, sizeof(out), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
		return out;
	}

	// ---------- HMAC-SHA256 / SHA256 (OpenSSL EVP) ----------
	inline std::string HmacSha256(const std::string& key, const std::string& msg)
	{
		unsigned char mac[EVP_MAX_MD_SIZE]; size_t len = 0;
		EVP_MAC_CTX* ctx = nullptr; EVP_MAC* mac_evp = nullptr;
		OSSL_PARAM params[2];
		mac_evp = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
		ctx = EVP_MAC_CTX_new(mac_evp);
		params[0] = OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0);
		params[1] = OSSL_PARAM_construct_end();
		EVP_MAC_init(ctx, reinterpret_cast<const unsigned char*>(key.data()), key.size(), params);
		EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(msg.data()), msg.size());
		EVP_MAC_final(ctx, mac, &len, sizeof(mac));
		EVP_MAC_CTX_free(ctx); EVP_MAC_free(mac_evp);
		return std::string(reinterpret_cast<char*>(mac), len);
	}

	inline std::string Sha256(const std::string& msg)
	{
		unsigned char h[32]; unsigned int len = 0;
		EVP_Digest(msg.data(), msg.size(), h, &len, EVP_sha256(), nullptr);
		return std::string(reinterpret_cast<char*>(h), 32);
	}

	// ---------- JWT HS256 (байт-в-байт совместим с ogni_wire.py jwt_sign/jwt_verify) ----------
	// Формат подписи в Python: sha256(header_b64 + '.' + payload_b64 + '.' + secret) — это НЕ стандартный HMAC,
	// а конкатенация. Копируем точно так же, чтобы стенд и прод были взаимозаменяемы.
	inline std::string JwtSign(const JsonPtr& claims, const std::string& secret)
	{
		JsonPtr hdr = Json::MakeObj();
		hdr->SetS("alg", "HS256"); hdr->SetS("typ", "JWT");
		std::string h = B64UrlEncode(reinterpret_cast<const uint8_t*>(hdr->Dump().data()), hdr->Dump().size());
		std::string c = B64UrlEncode(reinterpret_cast<const uint8_t*>(claims->Dump().data()), claims->Dump().size());
		std::string sigraw = Sha256(h + "." + c + "." + secret); // как в Python: hashlib.sha256(h+'.'+c+'.'+secret)
		std::string sig = B64UrlEncode(reinterpret_cast<const uint8_t*>(sigraw.data()), sigraw.size());
		return h + "." + c + "." + sig;
	}

	inline bool JwtVerify(const std::string& token, const std::string& secret, JsonPtr& out_claims)
	{
		size_t d1 = token.find('.');
		if (d1 == std::string::npos) return false;
		size_t d2 = token.find('.', d1 + 1);
		if (d2 == std::string::npos) return false;
		std::string h = token.substr(0, d1), c = token.substr(d1 + 1, d2 - d1 - 1), sig = token.substr(d2 + 1);
		std::string expect_raw = Sha256(h + "." + c + "." + secret);
		std::string expect = B64UrlEncode(reinterpret_cast<const uint8_t*>(expect_raw.data()), expect_raw.size());
		if (expect.size() != sig.size()) return false;
		// constant-time compare
		unsigned char diff = 0;
		for (size_t i = 0; i < sig.size(); ++i) diff |= static_cast<unsigned char>(expect[i] ^ sig[i]);
		if (diff) return false;
		auto dec = B64UrlDecode(c);
		out_claims = Json::ParseStrict(std::string(dec.begin(), dec.end()));
		if (!out_claims) return false;
		int64_t exp = JGetI(out_claims, "exp", 0);
		if (exp < int64_t(time(nullptr))) return false;
		return true;
	}

	// ---------- Пароли: Argon2id + PBKDF2-легаси (миграция при входе) ----------
	struct PwResult { enum Kind { Bad, Ok, Migrate } kind; };

	inline std::string HashArgon2id(const std::string& pw)
	{
		uint8_t salt[16]; RAND_bytes(salt, 16);
		char encoded[256];
		int rc = argon2id_hash_encoded(3, 65536, 4, pw.data(), pw.size(), salt, 16, 32, encoded, sizeof(encoded));
		if (rc != ARGON2_OK) return {};
		return encoded; // "$argon2id$v=19$m=65536,t=3,p=4$<b64salt>$<b64hash>"
	}

	inline bool VerifyArgon2id(const std::string& stored, const std::string& pw)
	{
		return argon2id_verify(stored.c_str(), pw.data(), pw.size()) == ARGON2_OK;
	}

	inline std::string HashPbkdf2Legacy(const std::string& pw) // только для генерации тестовых легаси-хэшей
	{
		std::vector<uint8_t> saltb = RandomBytes(16);
		std::string salt_hex = ToHex(std::string(saltb.begin(), saltb.end()));
		unsigned char dk[32];
		PKCS5_PBKDF2_HMAC(pw.data(), int(pw.size()), saltb.data(), 16, 200000, EVP_sha256(), 32, dk);
		return "pbkdf2$200000$" + salt_hex + "$" + ToHex(std::string(reinterpret_cast<char*>(dk), 32));
	}

	// Возвращает Ok/Migrate/Bad — зеркально check_pw() из authserver.py
	inline PwResult CheckPassword(const std::string& pw, const std::string& stored)
	{
		if (stored.rfind("$argon2", 0) == 0)
			return { VerifyArgon2id(stored, pw) ? PwResult::Ok : PwResult::Bad };
		// pbkdf2$iters$salt$dk
		size_t p1 = stored.find('$'), p2 = stored.find('$', p1 + 1), p3 = stored.find('$', p2 + 1);
		if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return { PwResult::Bad };
		std::string iters_s = stored.substr(p1 + 1, p2 - p1 - 1);
		std::string salt_hex = stored.substr(p2 + 1, p3 - p2 - 1);
		std::string dk_hex = stored.substr(p3 + 1);
		std::string salt; if (!FromHex(salt_hex, salt)) return { PwResult::Bad };
		std::string want; if (!FromHex(dk_hex, want)) return { PwResult::Bad };
		std::vector<unsigned char> dk(want.size());
		if (PKCS5_PBKDF2_HMAC(pw.data(), int(pw.size()),
			reinterpret_cast<const unsigned char*>(salt.data()), int(salt.size()),
			std::atoi(iters_s.c_str()), EVP_sha256(), int(want.size()), dk.data()) != 1)
			return { PwResult::Bad };
		if (CRYPTO_memcmp(dk.data(), want.data(), want.size()) == 0)
			return { PwResult::Migrate }; // верный, но легаси — переписать на Argon2id
		return { PwResult::Bad };
	}
} // namespace ogni
