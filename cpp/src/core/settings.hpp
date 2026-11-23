#pragma once

#include "json.hpp"
#include <string>

namespace np {

class Settings {
public:
    void load();
    void save();

    double get_double(const std::string& key, double def = 0.0) const;
    int get_int(const std::string& key, int def = 0) const;
    bool get_bool(const std::string& key, bool def = false) const;
    std::string get_string(const std::string& key, const std::string& def = "") const;

    void set(const std::string& key, const JsonValue& val);

private:
    std::wstring path_;
    JsonObject data_;
    void init_defaults();
};

} // namespace np
