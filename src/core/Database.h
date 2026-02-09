#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include "Logger.h"
#include "sqlite3.h"

namespace LCHBOT {

struct DbValue {
    enum class Type { Null, Integer, Real, Text, Blob };
    Type type = Type::Null;
    int64_t int_val = 0;
    double real_val = 0.0;
    std::string text_val;
    std::vector<uint8_t> blob_val;
    
    DbValue() : type(Type::Null) {}
    DbValue(int64_t v) : type(Type::Integer), int_val(v) {}
    DbValue(double v) : type(Type::Real), real_val(v) {}
    DbValue(const std::string& v) : type(Type::Text), text_val(v) {}
    DbValue(const char* v) : type(Type::Text), text_val(v) {}
    DbValue(const std::vector<uint8_t>& v) : type(Type::Blob), blob_val(v) {}
    
    bool isNull() const { return type == Type::Null; }
    int64_t toInt() const { return int_val; }
    double toReal() const { return real_val; }
    std::string toText() const { return text_val; }
    const std::vector<uint8_t>& toBlob() const { return blob_val; }
};

using DbRow = std::map<std::string, DbValue>;
using DbResult = std::vector<DbRow>;

class Database {
public:
    static Database& instance() {
        static Database inst;
        return inst;
    }
    
    ~Database() { close(); }
    
    bool open(const std::string& db_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        db_path_ = db_path;
        std::filesystem::path path(db_path);
        std::filesystem::create_directories(path.parent_path());
        
        OldData old_data;
        if (std::filesystem::exists(db_path)) {
            std::ifstream test(db_path);
            std::string first_line;
            if (std::getline(test, first_line) && first_line.size() >= 6 && first_line.substr(0, 6) == "TABLE:") {
                test.close();
                LOG_INFO("[Database] Detected old text format, migrating to SQLite...");
                old_data = readOldFormat(db_path);
                std::string backup = db_path + ".bak";
                try {
                    if (std::filesystem::exists(backup)) std::filesystem::remove(backup);
                    std::filesystem::rename(db_path, backup);
                    LOG_INFO("[Database] Old database backed up to: " + backup);
                } catch (const std::exception& e) {
                    LOG_ERROR("[Database] Backup failed: " + std::string(e.what()));
                    return false;
                }
            } else {
                test.close();
            }
        }
        
        int rc = sqlite3_open(db_path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            LOG_ERROR("[Database] Failed to open: " + std::string(sqlite3_errmsg(db_)));
            return false;
        }
        
        execRaw("PRAGMA journal_mode=WAL");
        execRaw("PRAGMA synchronous=NORMAL");
        
        if (!old_data.tables.empty()) {
            importOldData(old_data);
        }
        
        opened_ = true;
        LOG_INFO("[Database] Opened: " + db_path);
        return true;
    }
    
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        opened_ = false;
    }
    
    bool execute(const std::string& sql) {
        std::lock_guard<std::mutex> lock(mutex_);
        return execRaw(sql);
    }
    
    bool execute(const std::string& sql, const std::vector<DbValue>& params) {
        std::lock_guard<std::mutex> lock(mutex_);
        return execParams(sql, params);
    }
    
    DbResult query(const std::string& sql) {
        std::lock_guard<std::mutex> lock(mutex_);
        return queryInternal(sql, {});
    }
    
    DbResult query(const std::string& sql, const std::vector<DbValue>& params) {
        std::lock_guard<std::mutex> lock(mutex_);
        return queryInternal(sql, params);
    }
    
    int64_t lastInsertId() const { return last_insert_id_; }
    int affectedRows() const { return affected_rows_; }
    
    bool tableExists(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return exists;
    }
    
    bool beginTransaction() {
        std::lock_guard<std::mutex> lock(mutex_);
        return execRaw("BEGIN TRANSACTION");
    }
    
    bool commit() {
        std::lock_guard<std::mutex> lock(mutex_);
        return execRaw("COMMIT");
    }
    
    bool rollback() {
        std::lock_guard<std::mutex> lock(mutex_);
        return execRaw("ROLLBACK");
    }
    
    struct TableSchema {
        std::string name;
        std::vector<std::pair<std::string, std::string>> columns;
        std::string primary_key;
        std::vector<std::string> indexes;
    };
    
    TableSchema getTableSchema(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        TableSchema schema;
        schema.name = table_name;
        if (!db_) return schema;
        
        sqlite3_stmt* stmt = nullptr;
        std::string sql = "PRAGMA table_info(" + table_name + ")";
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* cn = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* ct = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            std::string col_name = cn ? cn : "";
            std::string col_type = ct ? ct : "";
            int pk = sqlite3_column_int(stmt, 5);
            schema.columns.emplace_back(col_name, col_type);
            if (pk) schema.primary_key = col_name;
        }
        sqlite3_finalize(stmt);
        return schema;
    }
    
    std::vector<std::string> getTableNames() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        if (!db_) return names;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (n) names.push_back(n);
        }
        sqlite3_finalize(stmt);
        return names;
    }
    
    int64_t getTableRowCount(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;
        auto result = queryInternal("SELECT COUNT(*) as cnt FROM " + table_name, {});
        if (!result.empty() && result[0].count("cnt")) {
            return result[0].at("cnt").toInt();
        }
        return 0;
    }
    
    void vacuum() {
        std::lock_guard<std::mutex> lock(mutex_);
        execRaw("VACUUM");
    }

private:
    Database() = default;
    
    bool execRaw(const std::string& sql) {
        if (!db_) return false;
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            if (err) {
                std::string msg = err;
                sqlite3_free(err);
                if (msg.find("already exists") == std::string::npos) {
                    LOG_ERROR("[Database] SQL error: " + msg + " | SQL: " + sql.substr(0, 100));
                }
            }
            return false;
        }
        affected_rows_ = sqlite3_changes(db_);
        last_insert_id_ = sqlite3_last_insert_rowid(db_);
        return true;
    }
    
    bool execParams(const std::string& sql, const std::vector<DbValue>& params) {
        if (!db_) return false;
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LOG_ERROR("[Database] Prepare error: " + std::string(sqlite3_errmsg(db_)));
            return false;
        }
        bindValues(stmt, params);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            LOG_ERROR("[Database] Execute error: " + std::string(sqlite3_errmsg(db_)));
            return false;
        }
        affected_rows_ = sqlite3_changes(db_);
        last_insert_id_ = sqlite3_last_insert_rowid(db_);
        return true;
    }
    
    DbResult queryInternal(const std::string& sql, const std::vector<DbValue>& params) {
        DbResult result;
        if (!db_) return result;
        
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LOG_ERROR("[Database] Query error: " + std::string(sqlite3_errmsg(db_)) + " | SQL: " + sql.substr(0, 100));
            return result;
        }
        
        if (!params.empty()) bindValues(stmt, params);
        
        int col_count = sqlite3_column_count(stmt);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            DbRow row;
            for (int i = 0; i < col_count; i++) {
                const char* name = sqlite3_column_name(stmt, i);
                if (!name) continue;
                std::string col_name(name);
                
                switch (sqlite3_column_type(stmt, i)) {
                    case SQLITE_INTEGER:
                        row[col_name] = DbValue(static_cast<int64_t>(sqlite3_column_int64(stmt, i)));
                        break;
                    case SQLITE_FLOAT:
                        row[col_name] = DbValue(sqlite3_column_double(stmt, i));
                        break;
                    case SQLITE_TEXT: {
                        const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                        row[col_name] = DbValue(std::string(t ? t : ""));
                        break;
                    }
                    case SQLITE_BLOB: {
                        const uint8_t* data = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, i));
                        int sz = sqlite3_column_bytes(stmt, i);
                        row[col_name] = DbValue(std::vector<uint8_t>(data, data + sz));
                        break;
                    }
                    default:
                        row[col_name] = DbValue();
                        break;
                }
            }
            result.push_back(std::move(row));
        }
        sqlite3_finalize(stmt);
        return result;
    }
    
    void bindValues(sqlite3_stmt* stmt, const std::vector<DbValue>& params) {
        for (size_t i = 0; i < params.size(); i++) {
            int idx = static_cast<int>(i + 1);
            const auto& p = params[i];
            switch (p.type) {
                case DbValue::Type::Null:    sqlite3_bind_null(stmt, idx); break;
                case DbValue::Type::Integer: sqlite3_bind_int64(stmt, idx, p.int_val); break;
                case DbValue::Type::Real:    sqlite3_bind_double(stmt, idx, p.real_val); break;
                case DbValue::Type::Text:    sqlite3_bind_text(stmt, idx, p.text_val.c_str(), -1, SQLITE_TRANSIENT); break;
                case DbValue::Type::Blob:    sqlite3_bind_blob(stmt, idx, p.blob_val.data(), static_cast<int>(p.blob_val.size()), SQLITE_TRANSIENT); break;
            }
        }
    }
    
    // --- Old text format migration ---
    
    struct OldTable {
        std::string name;
        std::vector<std::pair<std::string, std::string>> columns;
        std::string primary_key;
        int64_t auto_increment = 1;
        std::vector<DbRow> rows;
    };
    
    struct OldData {
        std::map<std::string, OldTable> tables;
    };
    
    OldData readOldFormat(const std::string& path) {
        OldData data;
        std::ifstream file(path);
        if (!file.is_open()) return data;
        
        std::string line, current;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            if (line.size() >= 6 && line.substr(0, 6) == "TABLE:") {
                current = line.substr(6);
                data.tables[current].name = current;
            } else if (line.size() >= 8 && line.substr(0, 8) == "COLUMNS:") {
                std::istringstream iss(line.substr(8));
                std::string col;
                while (std::getline(iss, col, ',')) {
                    size_t colon = col.find(':');
                    if (colon != std::string::npos) {
                        data.tables[current].columns.emplace_back(
                            col.substr(0, colon), col.substr(colon + 1));
                    }
                }
            } else if (line.size() >= 3 && line.substr(0, 3) == "PK:") {
                data.tables[current].primary_key = line.substr(3);
            } else if (line.size() >= 5 && line.substr(0, 5) == "AUTO:") {
                try { data.tables[current].auto_increment = std::stoll(line.substr(5)); } catch (...) {}
            } else if (line.size() >= 4 && line.substr(0, 4) == "ROW:") {
                DbRow row;
                std::string raw = line.substr(4);
                size_t pos = 0;
                while (pos < raw.size()) {
                    size_t next = raw.find('\x1F', pos);
                    if (next == std::string::npos) next = raw.size();
                    std::string field = raw.substr(pos, next - pos);
                    size_t eq = field.find('=');
                    if (eq != std::string::npos) {
                        std::string col = field.substr(0, eq);
                        std::string val = field.substr(eq + 1);
                        if (val.empty() || val == "NULL") {
                            row[col] = DbValue();
                        } else if (val[0] == 'I') {
                            try { row[col] = DbValue(std::stoll(val.substr(1))); } catch (...) {}
                        } else if (val[0] == 'R') {
                            try { row[col] = DbValue(std::stod(val.substr(1))); } catch (...) {}
                        } else if (val[0] == 'T') {
                            row[col] = DbValue(unescapeOld(val.substr(1)));
                        }
                    }
                    pos = next + 1;
                }
                data.tables[current].rows.push_back(std::move(row));
            }
        }
        return data;
    }
    
    std::string unescapeOld(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.size(); i++) {
            if (str[i] == '\\' && i + 1 < str.size()) {
                if (str[i+1] == 'n') { result += '\n'; i++; }
                else if (str[i+1] == 'r') { result += '\r'; i++; }
                else if (str[i+1] == 'x' && i + 3 < str.size() && str.substr(i+2, 2) == "1F") {
                    result += '\x1F'; i += 3;
                }
                else result += str[i];
            } else {
                result += str[i];
            }
        }
        return result;
    }
    
    void importOldData(const OldData& data) {
        execRaw("BEGIN TRANSACTION");
        
        for (const auto& [name, table] : data.tables) {
            std::string create_sql = "CREATE TABLE IF NOT EXISTS " + name + " (";
            for (size_t i = 0; i < table.columns.size(); i++) {
                if (i > 0) create_sql += ", ";
                create_sql += table.columns[i].first + " " + table.columns[i].second;
                if (table.columns[i].first == table.primary_key) {
                    create_sql += " PRIMARY KEY";
                }
            }
            create_sql += ")";
            execRaw(create_sql);
            
            if (table.rows.empty() || table.columns.empty()) continue;
            
            std::string insert_sql = "INSERT INTO " + name + " (";
            std::string placeholders;
            for (size_t i = 0; i < table.columns.size(); i++) {
                if (i > 0) { insert_sql += ", "; placeholders += ", "; }
                insert_sql += table.columns[i].first;
                placeholders += "?";
            }
            insert_sql += ") VALUES (" + placeholders + ")";
            
            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db_, insert_sql.c_str(), -1, &stmt, nullptr);
            
            int imported = 0;
            for (const auto& row : table.rows) {
                sqlite3_reset(stmt);
                sqlite3_clear_bindings(stmt);
                
                for (size_t i = 0; i < table.columns.size(); i++) {
                    int idx = static_cast<int>(i + 1);
                    const std::string& col = table.columns[i].first;
                    
                    if (row.count(col) == 0 || row.at(col).isNull()) {
                        sqlite3_bind_null(stmt, idx);
                    } else {
                        const auto& v = row.at(col);
                        switch (v.type) {
                            case DbValue::Type::Integer: sqlite3_bind_int64(stmt, idx, v.int_val); break;
                            case DbValue::Type::Real:    sqlite3_bind_double(stmt, idx, v.real_val); break;
                            case DbValue::Type::Text:    sqlite3_bind_text(stmt, idx, v.text_val.c_str(), -1, SQLITE_TRANSIENT); break;
                            default: sqlite3_bind_null(stmt, idx); break;
                        }
                    }
                }
                if (sqlite3_step(stmt) == SQLITE_DONE) imported++;
            }
            sqlite3_finalize(stmt);
            LOG_INFO("[Database] Migrated '" + name + "': " + std::to_string(imported) + "/" + std::to_string(table.rows.size()) + " rows");
        }
        
        execRaw("CREATE INDEX IF NOT EXISTS idx_messages_context ON messages(context_key)");
        execRaw("CREATE INDEX IF NOT EXISTS idx_messages_timestamp ON messages(timestamp)");
        execRaw("CREATE INDEX IF NOT EXISTS idx_messages_ctx_ts ON messages(context_key, timestamp)");
        execRaw("COMMIT");
        
        LOG_INFO("[Database] Migration complete");
    }
    
    sqlite3* db_ = nullptr;
    std::string db_path_;
    bool opened_ = false;
    int64_t last_insert_id_ = 0;
    int affected_rows_ = 0;
    std::mutex mutex_;
};

}
