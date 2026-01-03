#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <memory>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include "Logger.h"

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
    
    bool open(const std::string& db_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        db_path_ = db_path;
        std::filesystem::path path(db_path);
        std::filesystem::create_directories(path.parent_path());
        
        loadDatabase();
        
        opened_ = true;
        LOG_INFO("[Database] Opened: " + db_path);
        return true;
    }
    
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (opened_) {
            saveDatabase();
            opened_ = false;
        }
    }
    
    bool execute(const std::string& sql) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!opened_) return false;
        
        if (sql.find("CREATE TABLE") != std::string::npos) {
            return executeCreateTable(sql);
        } else if (sql.find("CREATE INDEX") != std::string::npos) {
            return executeCreateIndex(sql);
        } else if (sql.find("INSERT") != std::string::npos) {
            return executeInsert(sql);
        } else if (sql.find("UPDATE") != std::string::npos) {
            return executeUpdate(sql);
        } else if (sql.find("DELETE") != std::string::npos) {
            return executeDelete(sql);
        }
        
        return false;
    }
    
    bool execute(const std::string& sql, const std::vector<DbValue>& params) {
        std::string bound_sql = bindParams(sql, params);
        return execute(bound_sql);
    }
    
    DbResult query(const std::string& sql) {
        std::lock_guard<std::mutex> lock(mutex_);
        DbResult result;
        if (!opened_) return result;
        
        return executeSelect(sql);
    }
    
    DbResult query(const std::string& sql, const std::vector<DbValue>& params) {
        std::string bound_sql = bindParams(sql, params);
        return query(bound_sql);
    }
    
    int64_t lastInsertId() const {
        return last_insert_id_;
    }
    
    int affectedRows() const {
        return affected_rows_;
    }
    
    bool tableExists(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        return tables_.count(table_name) > 0;
    }
    
    bool beginTransaction() {
        std::lock_guard<std::mutex> lock(mutex_);
        in_transaction_ = true;
        return true;
    }
    
    bool commit() {
        std::lock_guard<std::mutex> lock(mutex_);
        in_transaction_ = false;
        saveDatabase();
        return true;
    }
    
    bool rollback() {
        std::lock_guard<std::mutex> lock(mutex_);
        in_transaction_ = false;
        loadDatabase();
        return true;
    }
    
    struct TableSchema {
        std::string name;
        std::vector<std::pair<std::string, std::string>> columns;
        std::string primary_key;
        std::vector<std::string> indexes;
    };
    
    TableSchema getTableSchema(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tables_.count(table_name)) {
            return tables_[table_name].schema;
        }
        return TableSchema{};
    }
    
    std::vector<std::string> getTableNames() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, table] : tables_) {
            names.push_back(name);
        }
        return names;
    }
    
    int64_t getTableRowCount(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tables_.count(table_name)) {
            return tables_[table_name].rows.size();
        }
        return 0;
    }
    
    void vacuum() {
        std::lock_guard<std::mutex> lock(mutex_);
        saveDatabase();
    }

private:
    Database() = default;
    
    struct Table {
        TableSchema schema;
        std::vector<DbRow> rows;
        int64_t auto_increment = 1;
    };
    
    bool executeCreateTable(const std::string& sql) {
        size_t name_start = sql.find("TABLE");
        if (name_start == std::string::npos) return false;
        name_start += 5;
        
        while (name_start < sql.size() && (sql[name_start] == ' ' || sql[name_start] == '\t')) name_start++;
        
        if (sql.substr(name_start, 13) == "IF NOT EXISTS") {
            name_start += 13;
            while (name_start < sql.size() && (sql[name_start] == ' ' || sql[name_start] == '\t')) name_start++;
        }
        
        size_t name_end = sql.find('(', name_start);
        if (name_end == std::string::npos) return false;
        
        std::string table_name = sql.substr(name_start, name_end - name_start);
        table_name.erase(0, table_name.find_first_not_of(" \t\n\r"));
        table_name.erase(table_name.find_last_not_of(" \t\n\r") + 1);
        
        if (tables_.count(table_name)) return true;
        
        Table table;
        table.schema.name = table_name;
        
        size_t col_start = name_end + 1;
        size_t col_end = sql.rfind(')');
        std::string cols_str = sql.substr(col_start, col_end - col_start);
        
        std::vector<std::string> col_defs;
        int paren_depth = 0;
        std::string current;
        for (char c : cols_str) {
            if (c == '(') paren_depth++;
            else if (c == ')') paren_depth--;
            else if (c == ',' && paren_depth == 0) {
                col_defs.push_back(current);
                current.clear();
                continue;
            }
            current += c;
        }
        if (!current.empty()) col_defs.push_back(current);
        
        for (const auto& col_def : col_defs) {
            std::string def = col_def;
            def.erase(0, def.find_first_not_of(" \t\n\r"));
            def.erase(def.find_last_not_of(" \t\n\r") + 1);
            
            if (def.find("PRIMARY KEY") == 0) {
                size_t pk_start = def.find('(');
                size_t pk_end = def.find(')');
                if (pk_start != std::string::npos && pk_end != std::string::npos) {
                    table.schema.primary_key = def.substr(pk_start + 1, pk_end - pk_start - 1);
                }
                continue;
            }
            
            size_t space = def.find(' ');
            if (space != std::string::npos) {
                std::string col_name = def.substr(0, space);
                std::string col_type = def.substr(space + 1);
                
                size_t type_end = col_type.find(' ');
                if (type_end != std::string::npos) {
                    std::string rest = col_type.substr(type_end);
                    col_type = col_type.substr(0, type_end);
                    
                    if (rest.find("PRIMARY KEY") != std::string::npos) {
                        table.schema.primary_key = col_name;
                    }
                }
                
                table.schema.columns.emplace_back(col_name, col_type);
            }
        }
        
        tables_[table_name] = table;
        return true;
    }
    
    bool executeCreateIndex(const std::string& sql) {
        size_t on_pos = sql.find(" ON ");
        if (on_pos == std::string::npos) return false;
        
        size_t table_start = on_pos + 4;
        size_t table_end = sql.find('(', table_start);
        if (table_end == std::string::npos) return false;
        
        std::string table_name = sql.substr(table_start, table_end - table_start);
        table_name.erase(0, table_name.find_first_not_of(" \t"));
        table_name.erase(table_name.find_last_not_of(" \t") + 1);
        
        if (tables_.count(table_name)) {
            size_t idx_name_start = sql.find("INDEX") + 5;
            while (idx_name_start < on_pos && sql[idx_name_start] == ' ') idx_name_start++;
            
            if (sql.substr(idx_name_start, 13) == "IF NOT EXISTS") {
                idx_name_start += 13;
                while (idx_name_start < on_pos && sql[idx_name_start] == ' ') idx_name_start++;
            }
            
            std::string idx_name = sql.substr(idx_name_start, on_pos - idx_name_start);
            idx_name.erase(idx_name.find_last_not_of(" \t") + 1);
            
            tables_[table_name].schema.indexes.push_back(idx_name);
        }
        return true;
    }
    
    bool executeInsert(const std::string& sql) {
        size_t into_pos = sql.find("INTO");
        if (into_pos == std::string::npos) return false;
        
        size_t table_start = into_pos + 4;
        while (table_start < sql.size() && sql[table_start] == ' ') table_start++;
        
        size_t table_end = sql.find_first_of(" (", table_start);
        std::string table_name = sql.substr(table_start, table_end - table_start);
        
        if (!tables_.count(table_name)) return false;
        
        auto& table = tables_[table_name];
        
        std::vector<std::string> columns;
        size_t col_start = sql.find('(', table_end);
        size_t col_end = sql.find(')', col_start);
        size_t values_pos = sql.find("VALUES");
        
        if (col_start != std::string::npos && col_start < values_pos) {
            std::string cols_str = sql.substr(col_start + 1, col_end - col_start - 1);
            std::istringstream iss(cols_str);
            std::string col;
            while (std::getline(iss, col, ',')) {
                col.erase(0, col.find_first_not_of(" \t"));
                col.erase(col.find_last_not_of(" \t") + 1);
                columns.push_back(col);
            }
        } else {
            for (const auto& [name, type] : table.schema.columns) {
                columns.push_back(name);
            }
        }
        
        size_t val_start = sql.find('(', values_pos);
        size_t val_end = sql.rfind(')');
        std::string vals_str = sql.substr(val_start + 1, val_end - val_start - 1);
        
        std::vector<std::string> values;
        bool in_quote = false;
        std::string current;
        for (size_t i = 0; i < vals_str.size(); i++) {
            char c = vals_str[i];
            if (c == '\'' && (i == 0 || vals_str[i-1] != '\\')) {
                in_quote = !in_quote;
                current += c;
            } else if (c == ',' && !in_quote) {
                values.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) values.push_back(current);
        
        DbRow row;
        for (size_t i = 0; i < columns.size() && i < values.size(); i++) {
            std::string val = values[i];
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            
            if (val == "NULL" || val == "null") {
                row[columns[i]] = DbValue();
            } else if (val.front() == '\'' && val.back() == '\'') {
                row[columns[i]] = DbValue(val.substr(1, val.size() - 2));
            } else if (val.find('.') != std::string::npos) {
                row[columns[i]] = DbValue(std::stod(val));
            } else {
                row[columns[i]] = DbValue(std::stoll(val));
            }
        }
        
        if (!table.schema.primary_key.empty() && row.count(table.schema.primary_key) == 0) {
            row[table.schema.primary_key] = DbValue(table.auto_increment++);
        }
        
        table.rows.push_back(row);
        last_insert_id_ = table.auto_increment - 1;
        affected_rows_ = 1;
        
        if (!in_transaction_) saveDatabase();
        return true;
    }
    
    bool executeUpdate(const std::string& sql) {
        size_t table_start = sql.find("UPDATE") + 6;
        while (table_start < sql.size() && sql[table_start] == ' ') table_start++;
        
        size_t table_end = sql.find(" SET", table_start);
        std::string table_name = sql.substr(table_start, table_end - table_start);
        table_name.erase(table_name.find_last_not_of(" \t") + 1);
        
        if (!tables_.count(table_name)) return false;
        
        auto& table = tables_[table_name];
        
        size_t set_start = table_end + 4;
        size_t where_pos = sql.find(" WHERE");
        std::string set_clause = (where_pos != std::string::npos) ? 
            sql.substr(set_start, where_pos - set_start) : sql.substr(set_start);
        
        std::map<std::string, std::string> updates;
        std::istringstream iss(set_clause);
        std::string assignment;
        while (std::getline(iss, assignment, ',')) {
            size_t eq = assignment.find('=');
            if (eq != std::string::npos) {
                std::string col = assignment.substr(0, eq);
                std::string val = assignment.substr(eq + 1);
                col.erase(0, col.find_first_not_of(" \t"));
                col.erase(col.find_last_not_of(" \t") + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t") + 1);
                updates[col] = val;
            }
        }
        
        std::function<bool(const DbRow&)> filter = [](const DbRow&) { return true; };
        if (where_pos != std::string::npos) {
            filter = parseWhereClause(sql.substr(where_pos + 6));
        }
        
        affected_rows_ = 0;
        for (auto& row : table.rows) {
            if (filter(row)) {
                for (const auto& [col, val] : updates) {
                    if (val == "NULL" || val == "null") {
                        row[col] = DbValue();
                    } else if (val.front() == '\'' && val.back() == '\'') {
                        row[col] = DbValue(val.substr(1, val.size() - 2));
                    } else if (val.find('.') != std::string::npos) {
                        row[col] = DbValue(std::stod(val));
                    } else {
                        row[col] = DbValue(std::stoll(val));
                    }
                }
                affected_rows_++;
            }
        }
        
        if (!in_transaction_) saveDatabase();
        return true;
    }
    
    bool executeDelete(const std::string& sql) {
        size_t from_pos = sql.find("FROM");
        if (from_pos == std::string::npos) return false;
        
        size_t table_start = from_pos + 4;
        while (table_start < sql.size() && sql[table_start] == ' ') table_start++;
        
        size_t table_end = sql.find_first_of(" ;", table_start);
        if (table_end == std::string::npos) table_end = sql.size();
        std::string table_name = sql.substr(table_start, table_end - table_start);
        
        if (!tables_.count(table_name)) return false;
        
        auto& table = tables_[table_name];
        
        size_t where_pos = sql.find(" WHERE");
        std::function<bool(const DbRow&)> filter = [](const DbRow&) { return true; };
        if (where_pos != std::string::npos) {
            filter = parseWhereClause(sql.substr(where_pos + 6));
        }
        
        size_t old_size = table.rows.size();
        table.rows.erase(
            std::remove_if(table.rows.begin(), table.rows.end(), filter),
            table.rows.end()
        );
        affected_rows_ = old_size - table.rows.size();
        
        if (!in_transaction_) saveDatabase();
        return true;
    }
    
    DbResult executeSelect(const std::string& sql) {
        DbResult result;
        
        size_t from_pos = sql.find("FROM");
        if (from_pos == std::string::npos) return result;
        
        size_t table_start = from_pos + 4;
        while (table_start < sql.size() && sql[table_start] == ' ') table_start++;
        
        size_t table_end = sql.find_first_of(" ;", table_start);
        if (table_end == std::string::npos) table_end = sql.size();
        std::string table_name = sql.substr(table_start, table_end - table_start);
        
        if (!tables_.count(table_name)) return result;
        
        auto& table = tables_[table_name];
        
        size_t select_end = from_pos;
        std::string select_clause = sql.substr(6, select_end - 6);
        select_clause.erase(0, select_clause.find_first_not_of(" \t"));
        select_clause.erase(select_clause.find_last_not_of(" \t") + 1);
        
        std::vector<std::string> columns;
        if (select_clause == "*") {
            for (const auto& [name, type] : table.schema.columns) {
                columns.push_back(name);
            }
        } else {
            std::istringstream iss(select_clause);
            std::string col;
            while (std::getline(iss, col, ',')) {
                col.erase(0, col.find_first_not_of(" \t"));
                col.erase(col.find_last_not_of(" \t") + 1);
                columns.push_back(col);
            }
        }
        
        size_t where_pos = sql.find(" WHERE");
        std::function<bool(const DbRow&)> filter = [](const DbRow&) { return true; };
        if (where_pos != std::string::npos) {
            size_t where_end = sql.find(" ORDER", where_pos);
            if (where_end == std::string::npos) where_end = sql.find(" LIMIT", where_pos);
            std::string where_clause = (where_end != std::string::npos) ?
                sql.substr(where_pos + 6, where_end - where_pos - 6) : sql.substr(where_pos + 6);
            filter = parseWhereClause(where_clause);
        }
        
        for (const auto& row : table.rows) {
            if (filter(row)) {
                DbRow selected_row;
                for (const auto& col : columns) {
                    if (row.count(col)) {
                        selected_row[col] = row.at(col);
                    }
                }
                result.push_back(selected_row);
            }
        }
        
        size_t order_pos = sql.find(" ORDER BY");
        if (order_pos != std::string::npos) {
            size_t col_start = order_pos + 9;
            while (col_start < sql.size() && sql[col_start] == ' ') col_start++;
            size_t col_end = sql.find_first_of(" ;", col_start);
            if (col_end == std::string::npos) col_end = sql.size();
            std::string order_col = sql.substr(col_start, col_end - col_start);
            
            bool desc = sql.find(" DESC", order_pos) != std::string::npos;
            
            std::sort(result.begin(), result.end(), [&](const DbRow& a, const DbRow& b) {
                if (!a.count(order_col) || !b.count(order_col)) return false;
                const auto& va = a.at(order_col);
                const auto& vb = b.at(order_col);
                bool less = false;
                if (va.type == DbValue::Type::Integer && vb.type == DbValue::Type::Integer) {
                    less = va.int_val < vb.int_val;
                } else if (va.type == DbValue::Type::Text && vb.type == DbValue::Type::Text) {
                    less = va.text_val < vb.text_val;
                }
                return desc ? !less : less;
            });
        }
        
        size_t limit_pos = sql.find(" LIMIT");
        if (limit_pos != std::string::npos) {
            size_t num_start = limit_pos + 6;
            while (num_start < sql.size() && sql[num_start] == ' ') num_start++;
            size_t num_end = sql.find_first_of(" ,;", num_start);
            if (num_end == std::string::npos) num_end = sql.size();
            int limit = std::stoi(sql.substr(num_start, num_end - num_start));
            
            int offset = 0;
            size_t offset_pos = sql.find(" OFFSET", limit_pos);
            if (offset_pos != std::string::npos) {
                size_t off_start = offset_pos + 7;
                while (off_start < sql.size() && sql[off_start] == ' ') off_start++;
                size_t off_end = sql.find_first_of(" ;", off_start);
                if (off_end == std::string::npos) off_end = sql.size();
                offset = std::stoi(sql.substr(off_start, off_end - off_start));
            }
            
            if (offset > 0 && offset < (int)result.size()) {
                result.erase(result.begin(), result.begin() + offset);
            }
            if (limit > 0 && limit < (int)result.size()) {
                result.resize(limit);
            }
        }
        
        return result;
    }
    
    std::function<bool(const DbRow&)> parseWhereClause(const std::string& clause) {
        std::string where = clause;
        where.erase(0, where.find_first_not_of(" \t"));
        where.erase(where.find_last_not_of(" \t;") + 1);
        
        size_t like_pos = where.find(" LIKE ");
        if (like_pos != std::string::npos) {
            std::string col = where.substr(0, like_pos);
            col.erase(0, col.find_first_not_of(" \t"));
            col.erase(col.find_last_not_of(" \t") + 1);
            
            std::string pattern = where.substr(like_pos + 6);
            pattern.erase(0, pattern.find_first_not_of(" \t'"));
            pattern.erase(pattern.find_last_not_of(" \t'") + 1);
            
            return [col, pattern](const DbRow& row) {
                if (!row.count(col)) return false;
                const auto& val = row.at(col);
                if (val.type != DbValue::Type::Text) return false;
                
                std::string text = val.text_val;
                std::string pat = pattern;
                
                if (pat.front() == '%' && pat.back() == '%') {
                    pat = pat.substr(1, pat.size() - 2);
                    return text.find(pat) != std::string::npos;
                } else if (pat.front() == '%') {
                    pat = pat.substr(1);
                    return text.size() >= pat.size() && text.substr(text.size() - pat.size()) == pat;
                } else if (pat.back() == '%') {
                    pat = pat.substr(0, pat.size() - 1);
                    return text.substr(0, pat.size()) == pat;
                }
                return text == pat;
            };
        }
        
        size_t eq_pos = where.find('=');
        if (eq_pos != std::string::npos) {
            std::string col = where.substr(0, eq_pos);
            std::string val = where.substr(eq_pos + 1);
            col.erase(0, col.find_first_not_of(" \t"));
            col.erase(col.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t'"));
            val.erase(val.find_last_not_of(" \t'") + 1);
            
            return [col, val](const DbRow& row) {
                if (!row.count(col)) return false;
                const auto& v = row.at(col);
                if (v.type == DbValue::Type::Text) return v.text_val == val;
                if (v.type == DbValue::Type::Integer) return std::to_string(v.int_val) == val;
                return false;
            };
        }
        
        return [](const DbRow&) { return true; };
    }
    
    std::string bindParams(const std::string& sql, const std::vector<DbValue>& params) {
        std::string result = sql;
        size_t param_idx = 0;
        size_t pos = 0;
        
        while ((pos = result.find('?', pos)) != std::string::npos && param_idx < params.size()) {
            std::string replacement;
            const auto& param = params[param_idx];
            
            switch (param.type) {
                case DbValue::Type::Null:
                    replacement = "NULL";
                    break;
                case DbValue::Type::Integer:
                    replacement = std::to_string(param.int_val);
                    break;
                case DbValue::Type::Real:
                    replacement = std::to_string(param.real_val);
                    break;
                case DbValue::Type::Text:
                    replacement = "'" + escapeString(param.text_val) + "'";
                    break;
                default:
                    replacement = "NULL";
                    break;
            }
            
            result.replace(pos, 1, replacement);
            pos += replacement.size();
            param_idx++;
        }
        
        return result;
    }
    
    std::string escapeString(const std::string& str) {
        std::string result;
        for (char c : str) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    }
    
    void loadDatabase() {
        std::ifstream file(db_path_);
        if (!file.is_open()) return;
        
        tables_.clear();
        std::string line;
        std::string current_table;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            if (line.substr(0, 6) == "TABLE:") {
                current_table = line.substr(6);
                tables_[current_table].schema.name = current_table;
            } else if (line.substr(0, 8) == "COLUMNS:") {
                std::string cols = line.substr(8);
                std::istringstream iss(cols);
                std::string col;
                while (std::getline(iss, col, ',')) {
                    size_t colon = col.find(':');
                    if (colon != std::string::npos) {
                        tables_[current_table].schema.columns.emplace_back(
                            col.substr(0, colon), col.substr(colon + 1));
                    }
                }
            } else if (line.substr(0, 3) == "PK:") {
                tables_[current_table].schema.primary_key = line.substr(3);
            } else if (line.substr(0, 5) == "AUTO:") {
                tables_[current_table].auto_increment = std::stoll(line.substr(5));
            } else if (line.substr(0, 4) == "ROW:") {
                DbRow row;
                std::string data = line.substr(4);
                std::istringstream iss(data);
                std::string field;
                while (std::getline(iss, field, '\x1F')) {
                    size_t eq = field.find('=');
                    if (eq != std::string::npos) {
                        std::string col = field.substr(0, eq);
                        std::string val = field.substr(eq + 1);
                        
                        if (val.empty() || val == "NULL") {
                            row[col] = DbValue();
                        } else if (val[0] == 'I') {
                            row[col] = DbValue(std::stoll(val.substr(1)));
                        } else if (val[0] == 'R') {
                            row[col] = DbValue(std::stod(val.substr(1)));
                        } else if (val[0] == 'T') {
                            row[col] = DbValue(unescapeString(val.substr(1)));
                        }
                    }
                }
                tables_[current_table].rows.push_back(row);
            }
        }
    }
    
    void saveDatabase() {
        std::ofstream file(db_path_);
        if (!file.is_open()) return;
        
        for (const auto& [name, table] : tables_) {
            file << "TABLE:" << name << "\n";
            file << "COLUMNS:";
            for (size_t i = 0; i < table.schema.columns.size(); i++) {
                if (i > 0) file << ",";
                file << table.schema.columns[i].first << ":" << table.schema.columns[i].second;
            }
            file << "\n";
            if (!table.schema.primary_key.empty()) {
                file << "PK:" << table.schema.primary_key << "\n";
            }
            file << "AUTO:" << table.auto_increment << "\n";
            
            for (const auto& row : table.rows) {
                file << "ROW:";
                bool first = true;
                for (const auto& [col, val] : row) {
                    if (!first) file << "\x1F";
                    first = false;
                    file << col << "=";
                    switch (val.type) {
                        case DbValue::Type::Null: file << "NULL"; break;
                        case DbValue::Type::Integer: file << "I" << val.int_val; break;
                        case DbValue::Type::Real: file << "R" << val.real_val; break;
                        case DbValue::Type::Text: file << "T" << escapeForStorage(val.text_val); break;
                        default: file << "NULL"; break;
                    }
                }
                file << "\n";
            }
            file << "\n";
        }
    }
    
    std::string escapeForStorage(const std::string& str) {
        std::string result;
        for (char c : str) {
            if (c == '\n') result += "\\n";
            else if (c == '\r') result += "\\r";
            else if (c == '\x1F') result += "\\x1F";
            else result += c;
        }
        return result;
    }
    
    std::string unescapeString(const std::string& str) {
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
    
    std::string db_path_;
    std::map<std::string, Table> tables_;
    bool opened_ = false;
    bool in_transaction_ = false;
    int64_t last_insert_id_ = 0;
    int affected_rows_ = 0;
    std::mutex mutex_;
};

}
