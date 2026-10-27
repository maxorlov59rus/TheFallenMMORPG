// Copyright «Огни душ Падших Валькирий» project. All Rights Reserved.
// Тонкая обёртка над MariaDB C API (mysql.h): подключение на поток, подготовленные запросы,
// маппинг строк в Json DOM — чтобы серверный код был зеркалом Python-референса.
#pragma once

#include <mysql.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include "ogni_json.h"

namespace ogni
{
	class Db
	{
	public:
		Db(const std::string& host, const std::string& user, const std::string& pw, const std::string& dbname)
		{
			m = mysql_init(nullptr);
			if (!m) throw std::runtime_error("mysql_init failed");
			unsigned int timeout = 5;
			mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
			if (!mysql_real_connect(m, host.c_str(), user.c_str(), pw.c_str(), dbname.c_str(), 0, nullptr, 0))
			{
				std::string err = mysql_error(m);
				mysql_close(m); m = nullptr;
				throw std::runtime_error("connect " + dbname + ": " + err);
			}
			mysql_set_character_set(m, "utf8mb4");
		}
		~Db() { if (m) mysql_close(m); }
		Db(const Db&) = delete;
		Db& operator=(const Db&) = delete;

		struct Row { std::vector<std::string> vals; std::vector<bool> nulls; };

		// Выполнить запрос с параметрами-плейсхолдерами '?' (позиционные).
		std::vector<Row> Query(const std::string& sql, const std::vector<std::string>& params = {})
		{
			MYSQL_STMT* st = mysql_stmt_init(m);
			if (mysql_stmt_prepare(st, sql.data(), unsigned(sql.size())) != 0)
			{ std::string e = mysql_stmt_error(st); mysql_stmt_close(st); throw std::runtime_error("prepare: " + e); }

			std::vector<MYSQL_BIND> binds(params.size());
			std::vector<unsigned long> lens(params.size());
			for (size_t i = 0; i < params.size(); ++i)
			{
				memset(&binds[i], 0, sizeof(binds[i]));
				binds[i].buffer_type = MYSQL_TYPE_STRING;
				binds[i].buffer = const_cast<char*>(params[i].data());
				lens[i] = static_cast<unsigned long>(params[i].size());
				binds[i].buffer_length = lens[i];
				binds[i].length = &lens[i];
			}
			if (!params.empty())
			{
				if (mysql_stmt_bind_param(st, binds.data()) != 0)
				{ std::string e = mysql_stmt_error(st); mysql_stmt_close(st); throw std::runtime_error("bind_param: " + e); }
			}
			if (mysql_stmt_execute(st) != 0)
			{ std::string e = mysql_stmt_error(st); mysql_stmt_close(st); throw std::runtime_error("execute: " + e); }

			std::vector<Row> rows;
			MYSQL_RES* res = mysql_stmt_result_metadata(st);
			if (res)
			{
				unsigned nfields = mysql_num_fields(res);
				std::vector<MYSQL_BIND> out(nfields);
				std::vector<std::vector<char>> bufs(nfields);
				std::vector<unsigned long> olen(nfields), omax(nfields);
				std::vector<char> isnull(nfields, 0);
				for (unsigned i = 0; i < nfields; ++i)
				{
					MYSQL_FIELD* f = mysql_fetch_field(res);
					size_t cap = std::max<size_t>(f->length > 0 ? f->length : 64, 64);
					if (f->type == MYSQL_TYPE_JSON || f->type == MYSQL_TYPE_BLOB || f->type == MYSQL_TYPE_LONG_BLOB ||
						f->type == MYSQL_TYPE_MEDIUM_BLOB) cap = 65536;
					bufs[i].assign(cap + 1, 0);
					memset(&out[i], 0, sizeof(out[i]));
					out[i].buffer_type = MYSQL_TYPE_STRING;
					out[i].buffer = bufs[i].data();
					out[i].buffer_length = unsigned(cap);
					out[i].length = &olen[i];
					out[i].is_null = &isnull[i];
					omax[i] = cap;
				}
				mysql_stmt_bind_result(st, out.data());
				while (mysql_stmt_fetch(st) == 0)
				{
					Row r;
					for (unsigned i = 0; i < nfields; ++i)
					{
						r.nulls.push_back(isnull[i] != 0);
						r.vals.push_back(isnull[i] ? std::string() : std::string(bufs[i].begin(), bufs[i].begin() + std::min<size_t>(olen[i], omax[i])));
					}
					rows.push_back(std::move(r));
				}
				mysql_free_result(res);
			}
			mysql_stmt_close(st);
			return rows;
		}

		uint64_t InsertReturnId(const std::string& sql, const std::vector<std::string>& params = {})
		{
			Query(sql, params);
			return mysql_insert_id(m);
		}

		void Execute(const std::string& sql, const std::vector<std::string>& params = {}) { Query(sql, params); }

		// Конвертация строки в объект JSON по списку имён колонок (порядок = SELECT)
		static JsonPtr ToJson(const Row& r, const std::vector<std::string>& cols)
		{
			JsonPtr o = Json::MakeObj();
			for (size_t i = 0; i < cols.size() && i < r.vals.size(); ++i)
			{
				if (r.nulls[i]) { o->Set(cols[i], Json::MakeNull()); continue; }
				const std::string& v = r.vals[i];
				if (!v.empty() && (v[0] == '{' || v[0] == '['))
				{
					JsonPtr j = Json::ParseStrict(v);
					o->Set(cols[i], j ? j : Json::MakeStr(v));
				}
				else
				{
					// числа без кавычек, если выглядят как число
					char* end = nullptr;
					double d = strtod(v.c_str(), &end);
					if (end && *end == 0 && !v.empty()) o->SetN(cols[i], d);
					else o->SetS(cols[i], v);
				}
			}
			return o;
		}

	private:
		MYSQL* m = nullptr;
	};
} // namespace ogni
