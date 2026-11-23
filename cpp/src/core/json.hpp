#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace np {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;
    Storage data{nullptr};

    JsonValue() = default;
    JsonValue(std::nullptr_t) : data(nullptr) {}
    JsonValue(bool b) : data(b) {}
    JsonValue(int i) : data(static_cast<double>(i)) {}
    JsonValue(double d) : data(d) {}
    JsonValue(const char* s) : data(std::string(s ? s : "")) {}
    JsonValue(std::string s) : data(std::move(s)) {}
    JsonValue(JsonArray a) : data(std::move(a)) {}
    JsonValue(JsonObject o) : data(std::move(o)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_number() const { return std::holds_alternative<double>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_array() const { return std::holds_alternative<JsonArray>(data); }
    bool is_object() const { return std::holds_alternative<JsonObject>(data); }

    bool as_bool(bool def = false) const {
        if (auto* p = std::get_if<bool>(&data)) return *p;
        if (auto* p = std::get_if<double>(&data)) return *p != 0.0;
        return def;
    }

    double as_double(double def = 0.0) const {
        if (auto* p = std::get_if<double>(&data)) return *p;
        if (auto* p = std::get_if<bool>(&data)) return *p ? 1.0 : 0.0;
        return def;
    }

    int as_int(int def = 0) const {
        if (auto* p = std::get_if<double>(&data)) return static_cast<int>(*p);
        if (auto* p = std::get_if<bool>(&data)) return *p ? 1 : 0;
        return def;
    }

    std::string as_string(const std::string& def = "") const {
        if (auto* p = std::get_if<std::string>(&data)) return *p;
        return def;
    }

    const JsonArray& as_array() const {
        static const JsonArray empty;
        if (auto* p = std::get_if<JsonArray>(&data)) return *p;
        return empty;
    }

    const JsonObject& as_object() const {
        static const JsonObject empty;
        if (auto* p = std::get_if<JsonObject>(&data)) return *p;
        return empty;
    }

    JsonObject& as_object_mut() {
        if (!is_object()) data = JsonObject{};
        return std::get<JsonObject>(data);
    }

    const JsonValue& get(const std::string& key) const {
        static const JsonValue null_val;
        if (auto* o = std::get_if<JsonObject>(&data)) {
            auto it = o->find(key);
            if (it != o->end()) return it->second;
        }
        return null_val;
    }

    static bool parse(const std::string& text, JsonValue& out);
    static bool parse_file(const std::wstring& path, JsonValue& out);
    std::string dump(int indent = 1) const;
    bool save_file(const std::wstring& path, int indent = 1) const;
};

} // namespace np
