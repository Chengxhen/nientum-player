#include "settings.hpp"
#include "paths.hpp"

#include <filesystem>

namespace np {

void Settings::init_defaults() {
    data_.clear();
    data_["volume"] = 0.78;
    data_["muted"] = false;
    data_["viz"] = "footlights";
    data_["player_style"] = "card";
    data_["blur_cg"] = true;
    data_["preview_on_select"] = true;
    data_["preview_seconds"] = 20;
    data_["device"] = -1;
    data_["geometry"] = "";
    data_["last_track"] = "";
    data_["lp_angle"] = 0.0;
    data_["fps"] = 120;
    data_["resolution"] = 1080;
    data_["title_bg"] = "";
    data_["title_track"] = "";
}

void Settings::load() {
    init_defaults();
    path_ = (std::filesystem::path(paths::settings_dir()) / L"settings.json").wstring();
    JsonValue v;
    if (JsonValue::parse_file(path_, v) && v.is_object()) {
        for (const auto& [k, val] : v.as_object()) {
            data_[k] = val;
        }
    }
}

void Settings::save() {
    if (path_.empty()) return;
    JsonValue v(data_);
    v.save_file(path_, 1);
}

double Settings::get_double(const std::string& key, double def) const {
    auto it = data_.find(key);
    return (it != data_.end() && it->second.is_number()) ? it->second.as_double(def) : def;
}

int Settings::get_int(const std::string& key, int def) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        if (it->second.is_number()) return it->second.as_int(def);
    }
    return def;
}

bool Settings::get_bool(const std::string& key, bool def) const {
    auto it = data_.find(key);
    return (it != data_.end()) ? it->second.as_bool(def) : def;
}

std::string Settings::get_string(const std::string& key, const std::string& def) const {
    auto it = data_.find(key);
    return (it != data_.end() && it->second.is_string()) ? it->second.as_string(def) : def;
}

void Settings::set(const std::string& key, const JsonValue& val) {
    data_[key] = val;
    save();
}

} // namespace np
