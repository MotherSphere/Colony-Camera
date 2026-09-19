#pragma once
#include "core.h"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace settings {
inline bool Read(const std::filesystem::path& path, CameraConfig& output) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size < 0 || size > 512 * 1024) return false;
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    return file.read(bytes.data(), size) && cc_parse_config(
        reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), &output) == 0;
}

// Only called by an explicit menu save/export. Keep the previous file until the
// complete replacement has been flushed; never truncate the user's live INI.
inline bool Write(const std::filesystem::path& path, const CameraConfig& config) {
    std::size_t size = 0;
    if (cc_serialize_config(&config, nullptr, 0, &size) != 4 || size > 512 * 1024) return false;
    std::vector<unsigned char> bytes(size);
    if (cc_serialize_config(&config, bytes.data(), bytes.size(), &size)) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += L".tmp";
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ready = WriteFile(file, bytes.data(), static_cast<DWORD>(size), &written, nullptr)
        && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    bool success = false;
    if (ready) {
        if (std::filesystem::exists(path, error) && !error) {
            auto backup = path;
            backup += L".bak";
            success = ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr) != 0;
        } else if (!error) {
            success = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
        }
    }
    if (!success) DeleteFileW(temporary.c_str());
    return success;
}
}
