#include "paths.hpp"
#include "types.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace np::paths {

namespace {
std::wstring g_app_dir;
std::wstring g_data_dir;
std::wstring g_media_dir;
std::wstring g_settings_dir;
std::mutex g_log_mtx;

std::wstring norm_join(const fs::path& base, const std::wstring& rel) {
    std::wstring r = rel;
    std::replace(r.begin(), r.end(), L'/', L'\\');
    fs::path p(r);
    if (p.is_absolute()) return p.lexically_normal().wstring();
    return (base / p).lexically_normal().wstring();
}
} // namespace

void init() {
    wchar_t exe[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 4);
    fs::path app = fs::path(exe).parent_path();
    g_app_dir = app.wstring();

    // Locate data_dir cleanly: ./data -> ../NientumPlayer/_internal/data -> ../../源码/NientumPlayer/data
    fs::path candidates_data[] = {
        app / L"data",
        app.parent_path() / L"NientumPlayer" / L"_internal" / L"data",
        app.parent_path().parent_path() / L"源码" / L"NientumPlayer" / L"data",
        app / L"_internal" / L"data"
    };
    g_data_dir = candidates_data[0].wstring();
    for (const auto& c : candidates_data) {
        if (fs::exists(c) && fs::is_directory(c)) {
            g_data_dir = fs::weakly_canonical(c).wstring();
            break;
        }
    }

    // Locate media_dir cleanly: ./media -> ../NientumPlayer/media -> ../../源码
    fs::path candidates_media[] = {
        app / L"media",
        app.parent_path() / L"NientumPlayer" / L"media",
        app.parent_path().parent_path() / L"源码"
    };
    g_media_dir = candidates_media[0].wstring();
    for (const auto& c : candidates_media) {
        if (fs::exists(c / L"音乐") || fs::exists(c / L"CG")) {
            g_media_dir = fs::weakly_canonical(c).wstring();
            break;
        }
    }

    wchar_t* appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)) && appdata) {
        fs::path sdir = fs::path(appdata) / L"NientumPlayer";
        CoTaskMemFree(appdata);
        std::error_code ec;
        fs::create_directories(sdir, ec);
        g_settings_dir = sdir.wstring();
    } else {
        g_settings_dir = g_app_dir;
    }
}

const std::wstring& app_dir() { return g_app_dir; }
const std::wstring& data_dir() { return g_data_dir; }
const std::wstring& media_dir() { return g_media_dir; }
const std::wstring& settings_dir() { return g_settings_dir; }

std::wstring data_path(const std::wstring& rel) {
    return norm_join(fs::path(g_data_dir), rel);
}

std::wstring data_path_utf8(const std::string& rel) {
    return data_path(to_wide(rel));
}

std::wstring media_path(const std::wstring& rel) {
    return norm_join(fs::path(g_media_dir), rel);
}

std::wstring media_path_utf8(const std::string& rel) {
    return media_path(to_wide(rel));
}

bool file_exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool reveal_in_explorer(const std::wstring& p) {
    if (!file_exists(p) && !dir_exists(p)) return false;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(p.c_str());
    if (pidl) {
        HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        if (SUCCEEDED(hr)) return true;
    }
    fs::path fp(p);
    ShellExecuteW(nullptr, L"open", fp.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return true;
}

void log(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_log_mtx);
    if (g_settings_dir.empty()) return;
    std::wstring lp = (fs::path(g_settings_dir) / L"player_v2.log").wstring();
    HANDLE h = CreateFileW(lp.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char hdr[64];
    int n = snprintf(hdr, sizeof(hdr), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    DWORD w = 0;
    WriteFile(h, hdr, static_cast<DWORD>(n), &w, nullptr);
    WriteFile(h, msg.data(), static_cast<DWORD>(msg.size()), &w, nullptr);
    WriteFile(h, "\r\n", 2, &w, nullptr);
    CloseHandle(h);
}

} // namespace np::paths
