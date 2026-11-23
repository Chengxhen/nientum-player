#pragma once

#include <string>

namespace np::paths {

void init();

const std::wstring& app_dir();
const std::wstring& data_dir();
const std::wstring& media_dir();
const std::wstring& settings_dir();

std::wstring data_path(const std::wstring& rel);
std::wstring data_path_utf8(const std::string& rel);
std::wstring media_path(const std::wstring& rel);
std::wstring media_path_utf8(const std::string& rel);

bool file_exists(const std::wstring& p);
bool dir_exists(const std::wstring& p);
bool reveal_in_explorer(const std::wstring& p);

void log(const std::string& msg);

} // namespace np::paths
