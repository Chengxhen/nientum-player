#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace np {

struct Chapter {
    std::string code;
    std::string zh;
    std::string en;
    std::string label;
    int count = 0;
    std::string cg;
    std::vector<std::string> variants;

    std::string title() const { return label + " · " + zh; }
};

struct Track {
    std::string id;
    std::string act;
    std::string act_zh;
    std::string act_en;
    std::string act_label;
    int index = 0;
    std::string title;
    std::string artist;
    std::string album;
    std::string trackno;
    std::string path;
    double duration = 0.0;
    int sr = 44100;
    int bits = 16;
    int channels = 2;
    std::string cover;
    bool original = false;
    std::string cover_src;

    std::wstring abs_path() const;
    std::wstring cover_path() const;
    std::string act_title() const { return act_label + " · " + act_zh; }
    std::string duration_str() const;
    std::string quality() const;
    std::string cg_rel() const;
};

struct GalleryItem {
    std::string id;
    std::string title;
    std::string act;
    std::string variant;
    std::string char_name;
    int index = 0;
    std::string path;
    std::string thumb;
    int w = 0;
    int h = 0;

    std::wstring abs_path() const;
    std::wstring thumb_path() const;
};

class Library {
public:
    std::vector<Track> tracks;
    std::vector<Chapter> chapters;
    std::unordered_map<std::string, std::vector<GalleryItem>> gallery;

    std::unordered_map<std::string, std::vector<int>> by_act_indices;
    std::unordered_map<std::string, int> chapter_index;
    std::unordered_map<std::string, int> index_by_id;
    std::unordered_map<std::string, int> act_order;
    std::unordered_map<std::string, GalleryItem> curtain_preview;

    bool load();
    std::vector<const Track*> act_tracks(const std::string& code) const;
    const Chapter* chapter(const std::string& code) const;
    const Track* track_by_id(const std::string& id) const;
    const Track* next_track(const Track* current, int delta = 1) const;
};

} // namespace np
