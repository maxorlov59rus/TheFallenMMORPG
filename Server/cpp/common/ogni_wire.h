// Copyright «Огни душ Падших Валькирий» project. All Rights Reserved.
// ogni_wire — framing + реестр сообщений. Зеркалит Server/tools/ogni_wire.py (acceptance-спецификацию)
// и SQL/Core/04_wire_contracts.sql (msg_registry). Изменения — только синхронно с обеими.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ogni
{
	inline constexpr uint8_t  kMagic0 = 'O';
	inline constexpr uint8_t  kMagic1 = 'F';
	inline constexpr uint8_t  kWireVersion = 1;
	inline constexpr size_t   kHeaderLen = 10; // magic(2)+ver(1)+msg_id(2)+seq(4)+flags(1), little-endian

	inline constexpr uint8_t  kFlagAck        = 0x01;
	inline constexpr uint8_t  kFlagReliable   = 0x02;
	inline constexpr uint8_t  kFlagCompressed = 0x04;

	// Реестр сообщений (синхронно с msg_registry / ogni_wire.Msg)
	enum MsgId : uint16_t
	{
		MOVE_UPDATE       = 0x1001,
		SWING_BEGIN       = 0x1002,
		CAST_ABILITY      = 0x1003,
		FORGE_CRAFT       = 0x1004,
		COLLECT_SOUL_DROP = 0x1005,
		CLAN_PING         = 0x1006,
		SPAWN_ENTITY      = 0x2001,
		DESPAWN_ENTITY    = 0x2002,
		SWING_RESULT      = 0x2010,
		ABILITY_ACK       = 0x2011,
		WORLD_STATE       = 0x2020,
		DEATH_LAST_CHANCE = 0x2021,
		SOUL_DELTA        = 0x2030,
		FORGE_MENU        = 0x2031,
		REP_CHANGE        = 0x2040,
		SESSION_HELLO     = 0x2F02,
		SESSION_ACCEPT    = 0x2F00,
		PROTOCOL_MISMATCH = 0x2F01,
		CHAR_LIST_REQ     = 0x3001,
		CHAR_LIST         = 0x3002,
		CHAR_TEMPLATES    = 0x3003,
		CHAR_CREATE       = 0x3010,
		CHAR_CREATE_OK    = 0x3011,
		CHAR_ENTER_WORLD  = 0x3012,
		WORLD_READY       = 0x3013,
	};

	struct Frame
	{
		uint16_t msg_id = 0;
		uint32_t seq = 0;
		uint8_t  flags = 0;
		std::vector<uint8_t> body;
	};

	// Кодирование заголовка вручную (без зависимости от endianness платформы)
	inline void put_le16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
	inline void put_le32(uint8_t* p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }
	inline uint16_t get_le16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
	inline uint32_t get_le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }

	inline std::vector<uint8_t> PackFrame(uint16_t msg_id, uint32_t seq, const uint8_t* body, size_t body_len,
	                                      uint8_t flags = kFlagReliable)
	{
		std::vector<uint8_t> out(kHeaderLen + body_len);
		out[0] = kMagic0; out[1] = kMagic1; out[2] = kWireVersion;
		put_le16(&out[3], msg_id);
		put_le32(&out[5], seq);
		out[9] = flags;
		if (body_len) std::memcpy(&out[kHeaderLen], body, body_len);
		return out;
	}

	inline bool UnpackFrame(const uint8_t* data, size_t len, Frame& out)
	{
		if (len < kHeaderLen || data[0] != kMagic0 || data[1] != kMagic1 || data[2] != kWireVersion)
			return false;
		out.msg_id = get_le16(&data[3]);
		out.seq    = get_le32(&data[5]);
		out.flags  = data[9];
		out.body.assign(data + kHeaderLen, data + len);
		return true;
	}

	// Типизированные тела (по schema_json из БД)
	struct MoveUpdateBody { float pos[3]; float vel[3]; float yaw; uint64_t stamp_ms; };

	inline std::vector<uint8_t> EncodeMoveUpdate(const MoveUpdateBody& m)
	{
		std::vector<uint8_t> b(7 * sizeof(float) + sizeof(uint64_t));
		for (int i = 0; i < 3; ++i) std::memcpy(&b[i * 4], &m.pos[i], 4);
		for (int i = 0; i < 3; ++i) std::memcpy(&b[12 + i * 4], &m.vel[i], 4);
		std::memcpy(&b[24], &m.yaw, 4);
		uint64_t st = m.stamp_ms;
		std::memcpy(&b[28], &st, 8);
		return b;
	}

	inline bool DecodeMoveUpdate(const uint8_t* p, size_t len, MoveUpdateBody& m)
	{
		if (len < 36) return false;
		for (int i = 0; i < 3; ++i) std::memcpy(&m.pos[i], &p[i * 4], 4);
		for (int i = 0; i < 3; ++i) std::memcpy(&m.vel[i], &p[12 + i * 4], 4);
		std::memcpy(&m.yaw, &p[24], 4);
		std::memcpy(&m.stamp_ms, &p[28], 8);
		return true;
	}

	// S2C_SessionAccept: session_key_len(u8) + key bytes + aoi_seed(u32 LE)
	inline std::vector<uint8_t> EncodeSessionAccept(const std::vector<uint8_t>& session_key, uint32_t aoi_seed)
	{
		std::vector<uint8_t> b;
		b.push_back(static_cast<uint8_t>(session_key.size()));
		b.insert(b.end(), session_key.begin(), session_key.end());
		b.resize(b.size() + 4);
		put_le32(&b[b.size() - 4], aoi_seed);
		return b;
	}

	// S2C_SessionAccept (wire-вариант): len u16 LE + key + seed u32 — зеркало ogni_wire.enc_session_accept.
	// Используется прод-серверами; старый однобайтовый len оставлен для совместимости внутри cpp-пары.
	inline std::vector<uint8_t> EncodeSessionAcceptWire(const std::vector<uint8_t>& session_key, uint32_t aoi_seed)
	{
		std::vector<uint8_t> b(2);
		put_le16(b.data(), static_cast<uint16_t>(session_key.size()));
		b.insert(b.end(), session_key.begin(), session_key.end());
		b.resize(b.size() + 4);
		put_le32(&b[b.size() - 4], aoi_seed);
		return b;
	}
} // namespace ogni
