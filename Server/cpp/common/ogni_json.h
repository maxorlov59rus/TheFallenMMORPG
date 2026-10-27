// Copyright «Огни душ Падших Валькирий» project. All Rights Reserved.
// Мини-DOM JSON (parse/serialize) — без внешних зависимостей, переносим Win64/POSIX.
// Допущения: UTF-8, числа как double/int, ключи в порядке вставки. Достаточно для wire-контрактов.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ogni
{
	class Json;
	using JsonPtr = std::shared_ptr<Json>;

	class Json
	{
	public:
		enum class Type { Null, Bool, Number, String, Array, Object };

		Type type = Type::Null;
		bool b = false;
		double num = 0.0;
		std::string str;
		std::vector<JsonPtr> arr;
		// упорядоченные пары для объектов (сохраняем порядок ключей при сериализации)
		std::vector<std::pair<std::string, JsonPtr>> obj;

		static JsonPtr MakeNull() { auto j = std::make_shared<Json>(); return j; }
		static JsonPtr MakeBool(bool v) { auto j = std::make_shared<Json>(); j->type = Type::Bool; j->b = v; return j; }
		static JsonPtr MakeNum(double v) { auto j = std::make_shared<Json>(); j->type = Type::Number; j->num = v; return j; }
		static JsonPtr MakeStr(std::string v) { auto j = std::make_shared<Json>(); j->type = Type::String; j->str = std::move(v); return j; }
		static JsonPtr MakeArr() { auto j = std::make_shared<Json>(); j->type = Type::Array; return j; }
		static JsonPtr MakeObj() { auto j = std::make_shared<Json>(); j->type = Type::Object; return j; }

		JsonPtr& operator[](const std::string& key)
		{
			for (auto& kv : obj) if (kv.first == key) return kv.second;
			obj.emplace_back(key, MakeNull());
			return obj.back().second;
		}
		const JsonPtr* Find(const std::string& key) const
		{
			for (auto& kv : obj) if (kv.first == key) return &kv.second;
			return nullptr;
		}
		void Push(JsonPtr v) { arr.push_back(std::move(v)); }

		bool IsNull() const { return type == Type::Null; }
		int64_t AsInt() const { return static_cast<int64_t>(num); }
		std::string AsString() const { return type == Type::String ? str : std::string(); }

		void Set(const std::string& key, JsonPtr v) { (*this)[key] = std::move(v); }
		void SetS(const std::string& k, const std::string& v) { Set(k, MakeStr(v)); }
		void SetN(const std::string& k, double v) { Set(k, MakeNum(v)); }
		void SetB(const std::string& k, bool v) { Set(k, MakeBool(v)); }

		// -------- serialize --------
		std::string Dump() const
		{
			std::string out;
			WriteTo(out);
			return out;
		}

		void WriteTo(std::string& out) const
		{
			switch (type)
			{
			case Type::Null: out += "null"; break;
			case Type::Bool: out += b ? "true" : "false"; break;
			case Type::Number:
			{
				if (num == double(int64_t(num))) { char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)int64_t(num)); out += buf; }
				else { char buf[40]; snprintf(buf, sizeof(buf), "%.6g", num); out += buf; }
				break;
			}
			case Type::String: EscapeInto(out, str); break;
			case Type::Array:
				out += '[';
				for (size_t i = 0; i < arr.size(); ++i) { if (i) out += ','; arr[i]->WriteTo(out); }
				out += ']';
				break;
			case Type::Object:
				out += '{';
				for (size_t i = 0; i < obj.size(); ++i)
				{
					if (i) out += ',';
					EscapeInto(out, obj[i].first);
					out += ':';
					obj[i].second->WriteTo(out);
				}
				out += '}';
				break;
			}
		}

		static void EscapeInto(std::string& out, const std::string& s)
		{
			out += '"';
			for (char c : s)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if ((unsigned char)c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
					else out += c;
				}
			}
			out += '"';
		}

		// -------- parse --------
		static JsonPtr Parse(const std::string& text)
		{
			size_t pos = 0;
			JsonPtr res = ParseValue(text, pos);
			SkipWs(text, pos);
			return res; // если мусор после значения — считаем битым (возвращаем nullptr ниже)
		}

		static JsonPtr ParseStrict(const std::string& text)
		{
			size_t pos = 0;
			JsonPtr res = ParseValue(text, pos);
			SkipWs(text, pos);
			if (!res || pos != text.size()) return nullptr;
			return res;
		}

	private:
		static void SkipWs(const std::string& s, size_t& p) { while (p < s.size() && (s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r')) ++p; }

		static JsonPtr ParseValue(const std::string& s, size_t& p)
		{
			SkipWs(s, p);
			if (p >= s.size()) return nullptr;
			char c = s[p];
			if (c == '{') return ParseObject(s, p);
			if (c == '[') return ParseArray(s, p);
			if (c == 0x22) { return ParseString(s, p); }
			if (c == 't' || c == 'f') return ParseBool(s, p);
			if (c == 'n') return ParseNull(s, p);
			return ParseNumber(s, p);
		}

		static std::string ParseStringRaw(const std::string& s, size_t& p)
		{
			if (p >= s.size() || s[p] != '"') { p = std::string::npos; return {}; }
			++p;
			std::string out;
			while (p < s.size())
			{
				char c = s[p++];
				if (c == '"') return out;
				if (c == '\\')
				{
					if (p >= s.size()) break;
					char e = s[p++];
					switch (e)
					{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'u':
					{
						if (p + 4 > s.size()) { p = std::string::npos; return {}; }
						unsigned cp = std::stoul(s.substr(p, 4), nullptr, 16); p += 4;
						// минимальный UTF-8 encoder (без суррогатных пар — достаточно для имени/зон)
						if (cp < 0x80) out += char(cp);
						else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
						else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
						break;
					}
					default: p = std::string::npos; return {};
					}
				}
				else out += c;
			}
			p = std::string::npos;
			return {};
		}

		static JsonPtr ParseString(const std::string& s, size_t& p)
		{
			std::string v = ParseStringRaw(s, p);
			if (p == std::string::npos) return nullptr;
			return MakeStr(v);
		}

		static JsonPtr ParseNumber(const std::string& s, size_t& p)
		{
			size_t start = p;
			if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
			while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p]=='.' || s[p]=='e' || s[p]=='E' || ((s[p]=='+'||s[p]=='-') && p>start && (s[p-1]=='e'||s[p-1]=='E'))) ) ++p;
			if (p == start) return nullptr;
			try { return MakeNum(std::stod(s.substr(start, p - start))); }
			catch (...) { return nullptr; }
		}

		static JsonPtr ParseBool(const std::string& s, size_t& p)
		{
			if (s.compare(p, 4, "true") == 0) { p += 4; return MakeBool(true); }
			if (s.compare(p, 5, "false") == 0) { p += 5; return MakeBool(false); }
			return nullptr;
		}
		static JsonPtr ParseNull(const std::string& s, size_t& p)
		{
			if (s.compare(p, 4, "null") == 0) { p += 4; return MakeNull(); }
			return nullptr;
		}

		static JsonPtr ParseArray(const std::string& s, size_t& p)
		{
			auto a = MakeArr();
			++p; // [
			SkipWs(s, p);
			if (p < s.size() && s[p] == ']') { ++p; return a; }
			while (p != std::string::npos && p < s.size())
			{
				JsonPtr v = ParseValue(s, p);
				if (!v) return nullptr;
				a->Push(v);
				SkipWs(s, p);
				if (p < s.size() && s[p] == ',') { ++p; continue; }
				if (p < s.size() && s[p] == ']') { ++p; return a; }
				return nullptr;
			}
			return nullptr;
		}

		static JsonPtr ParseObject(const std::string& s, size_t& p)
		{
			auto o = MakeObj();
			++p; // {
			SkipWs(s, p);
			if (p < s.size() && s[p] == '}') { ++p; return o; }
			while (p != std::string::npos && p < s.size())
			{
				std::string key = ParseStringRaw(s, p);
				if (p == std::string::npos) return nullptr;
				SkipWs(s, p);
				if (p >= s.size() || s[p] != ':') return nullptr;
				++p;
				JsonPtr v = ParseValue(s, p);
				if (!v) return nullptr;
				o->Set(key, v);
				SkipWs(s, p);
				if (p < s.size() && s[p] == ',') { ++p; continue; }
				if (p < s.size() && s[p] == '}') { ++p; return o; }
				return nullptr;
			}
			return nullptr;
		}
	};

	// Хелперы чтения из разобранного JSON
	inline std::string JGetS(const JsonPtr& j, const char* key, const std::string& def = {})
	{
		if (!j) return def;
		if (const JsonPtr* v = j->Find(key)) if ((*v)->type == Json::Type::String) return (*v)->str;
		return def;
	}
	inline int64_t JGetI(const JsonPtr& j, const char* key, int64_t def = 0)
	{
		if (!j) return def;
		if (const JsonPtr* v = j->Find(key)) if ((*v)->type == Json::Type::Number) return (*v)->AsInt();
		return def;
	}
} // namespace ogni
