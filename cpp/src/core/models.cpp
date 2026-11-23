#include "models.hpp"
#include "json.hpp"
#include "paths.hpp"

#include <cmath>
#include <cstdio>

namespace np {

std::wstring Track::abs_path() const {
    return paths::media_path_utf8(path);
}

std::wstring Track::cover_path() const {
    return paths::data_path_utf8(cover);
}

std::string Track::duration_str() const {
    int s = static_cast<int>(std::llround(duration));
    if (s < 0) s = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
    return buf;
}

std::string Track::quality() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.0fkHz / %dbit", sr / 1000.0, bits);
    return buf;
}

std::string Track::cg_rel() const {
    std::string primary = "CG/谢幕/" + act + "_" + act_zh + ".png";
    if (paths::file_exists(paths::media_path_utf8(primary))) return primary;
    if (act == "ActT") return "CG/剧情/PZImgThankyouforplayingSmalltheater.png";
    return "CG/剧情/剧情_序章_舞台.png";
}

std::wstring GalleryItem::abs_path() const {
    return paths::media_path_utf8(path);
}

std::wstring GalleryItem::thumb_path() const {
    return paths::data_path_utf8(thumb);
}

bool Library::load() {
    tracks.clear();
    chapters.clear();
    gallery.clear();
    by_act_indices.clear();
    chapter_index.clear();
    index_by_id.clear();
    act_order.clear();
    curtain_preview.clear();

    JsonValue tv;
    if (JsonValue::parse_file(paths::data_path(L"tracks.json"), tv) && tv.is_array()) {
        for (const auto& item : tv.as_array()) {
            Track t;
            t.id = item.get("id").as_string();
            t.act = item.get("act").as_string();
            t.act_zh = item.get("act_zh").as_string();
            t.act_en = item.get("act_en").as_string();
            t.act_label = item.get("act_label").as_string();
            t.index = item.get("index").as_int();
            t.title = item.get("title").as_string();
            t.artist = item.get("artist").as_string();
            t.album = item.get("album").as_string();
            t.trackno = item.get("trackno").as_string();
            t.path = item.get("path").as_string();
            t.duration = item.get("duration").as_double();
            t.sr = item.get("sr").as_int(44100);
            t.bits = item.get("bits").as_int(16);
            t.channels = item.get("channels").as_int(2);
            t.cover = item.get("cover").as_string();
            t.original = item.get("original").as_bool(false);
            t.cover_src = item.get("cover_src").as_string();

            int idx = static_cast<int>(tracks.size());
            tracks.push_back(std::move(t));
            index_by_id[tracks.back().id] = idx;
            by_act_indices[tracks.back().act].push_back(idx);
        }
    }

    JsonValue cv;
    if (JsonValue::parse_file(paths::data_path(L"chapters.json"), cv) && cv.is_array()) {
        for (const auto& item : cv.as_array()) {
            Chapter c;
            c.code = item.get("code").as_string();
            c.zh = item.get("zh").as_string();
            c.en = item.get("en").as_string();
            c.label = item.get("label").as_string();
            c.count = item.get("count").as_int();
            c.cg = item.get("cg").as_string();
            for (const auto& v : item.get("variants").as_array()) {
                c.variants.push_back(v.as_string());
            }
            int idx = static_cast<int>(chapters.size());
            chapters.push_back(std::move(c));
            chapter_index[chapters.back().code] = idx;
            act_order[chapters.back().code] = idx;
        }
    }

    JsonValue gv;
    if (JsonValue::parse_file(paths::data_path(L"gallery.json"), gv) && gv.is_object()) {
        for (const auto& [cat, arr] : gv.as_object()) {
            auto& vec = gallery[cat];
            for (const auto& item : arr.as_array()) {
                GalleryItem g;
                g.id = item.get("id").as_string();
                g.title = item.get("title").as_string();
                g.act = item.get("act").as_string();
                g.variant = item.get("variant").as_string();
                g.char_name = item.get("char").as_string();
                g.index = item.get("index").as_int();
                g.path = item.get("path").as_string();
                g.thumb = item.get("thumb").as_string();
                g.w = item.get("w").as_int();
                g.h = item.get("h").as_int();
                vec.push_back(std::move(g));
            }
        }
    }

    auto it_curtain = gallery.find("curtain");
    if (it_curtain != gallery.end()) {
        for (const auto& item : it_curtain->second) {
            if (item.variant.empty() && !item.act.empty() && curtain_preview.find(item.act) == curtain_preview.end()) {
                curtain_preview[item.act] = item;
            }
        }
    }

    auto it_story = gallery.find("story");
    if (it_story != gallery.end()) {
        struct Fallback { const char* code; const char* id; };
        Fallback fallbacks[] = {
            {"ActT", "epilogue_universe_game"},
            {"其他", "story_005"},
            {"未分类", "story_005"},
        };
        for (const auto& fb : fallbacks) {
            if (curtain_preview.find(fb.code) == curtain_preview.end()) {
                for (const auto& item : it_story->second) {
                    if (item.id == fb.id) {
                        curtain_preview[fb.code] = item;
                        break;
                    }
                }
            }
        }
    }

    return !tracks.empty();
}

std::vector<const Track*> Library::act_tracks(const std::string& code) const {
    std::vector<const Track*> out;
    auto it = by_act_indices.find(code);
    if (it == by_act_indices.end()) return out;
    out.reserve(it->second.size());
    for (int idx : it->second) out.push_back(&tracks[idx]);
    return out;
}

const Chapter* Library::chapter(const std::string& code) const {
    auto it = chapter_index.find(code);
    if (it == chapter_index.end()) return nullptr;
    return &chapters[it->second];
}

const Track* Library::track_by_id(const std::string& id) const {
    auto it = index_by_id.find(id);
    if (it == index_by_id.end()) return nullptr;
    return &tracks[it->second];
}

const Track* Library::next_track(const Track* current, int delta) const {
    if (tracks.empty()) return nullptr;
    if (!current) return &tracks[0];
    auto it = index_by_id.find(current->id);
    if (it == index_by_id.end()) return &tracks[0];
    int n = static_cast<int>(tracks.size());
    int i = ((it->second + delta) % n + n) % n;
    return &tracks[i];
}

} // namespace np
