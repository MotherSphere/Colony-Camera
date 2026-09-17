#include "../plugin/config_store.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        const auto parent = fs::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            path = parent / (L"ColonyCamera-config-test-" + std::to_wstring(GetCurrentProcessId())
                + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            std::error_code error;
            if (fs::create_directory(path, error)) return;
            if (error) throw std::runtime_error("Cannot create isolated test directory");
        }
        throw std::runtime_error("Cannot reserve isolated test directory");
    }
    ~TemporaryDirectory() {
        // This exact directory was exclusively created above; never remove a
        // caller-supplied path, the shared temp directory or any game directory.
        std::error_code error;
        fs::remove_all(path, error);
    }
};
std::string Bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    Require(file.good(), "Expected test file is missing");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Put(const fs::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))), "Cannot write test fixture");
}
bool Same(const CameraConfig& a, const CameraConfig& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
void CheckRead(const fs::path& path, const CameraConfig& expected) {
    CameraConfig actual{};
    Require(settings::Read(path, actual), "Saved configuration cannot be read");
    Require(Same(actual, expected), "Saved configuration changed in roundtrip");
}
}
int main() {
    try {
        TemporaryDirectory isolated;
        const auto file = isolated.path / "nested" / "ColonyCamera.ini";
        auto backup = file; backup += L".bak";
        auto temporary = file; temporary += L".tmp";
        auto first = cc_defaults();
        Require(settings::Write(file, first), "Initial atomic create failed");
        CheckRead(file, first);
        Require(!fs::exists(backup) && !fs::exists(temporary), "Initial save left unexpected side files");
        const auto firstBytes = Bytes(file);

        auto second = first;
        second.profiles[CC_EXPLORATION].offset[0] = 32.125f;
        second.profiles[CC_SPRINT].fov_offset = 12.0f;
        second.locomotion_profiles |= 1u << CC_SPRINT;
        Require(settings::Write(file, second), "First replacement failed");
        CheckRead(file, second);
        Require(Bytes(backup) == firstBytes, "Backup does not preserve previous file exactly");
        Require(!fs::exists(temporary), "Replacement left temporary file");
        auto preceding = Bytes(file);
        for (unsigned revision = 0; revision < 8; ++revision) {
            second.profiles[CC_EXPLORATION].offset[2] = static_cast<float>(revision);
            Require(settings::Write(file, second), "Repeated replacement failed with existing backup");
            CheckRead(file, second);
            Require(Bytes(backup) == preceding, "Repeated save did not retain immediate previous version");
            Require(!fs::exists(temporary), "Repeated save left temporary file");
            preceding = Bytes(file);
        }
        const auto saved = Bytes(file);
        const auto savedBackup = Bytes(backup);

        auto invalid = second;
        invalid.profiles[CC_COMBAT].zoom = std::numeric_limits<float>::quiet_NaN();
        Require(!settings::Write(file, invalid), "Invalid configuration was persisted");
        Require(Bytes(file) == saved && Bytes(backup) == savedBackup, "Invalid save changed existing data");
        Require(!fs::exists(temporary), "Invalid save created a temporary file");

        Put(temporary, "unrelated temporary file must survive");
        Require(!settings::Write(file, first), "Existing temporary file was overwritten");
        Require(Bytes(temporary) == "unrelated temporary file must survive", "Existing temporary data was deleted or altered");
        Require(Bytes(file) == saved && Bytes(backup) == savedBackup, "Blocked save changed live configuration or backup");
        Require(fs::remove(temporary), "Could not remove owned test fixture");

        // Sharing denies deletion/replacement. The failed operation must leave
        // the live file intact and clean up only the temporary file it created.
        const auto locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "Could not lock test configuration");
        const bool replacedWhileLocked = settings::Write(file, first);
        CloseHandle(locked);
        Require(!replacedWhileLocked, "Replacement unexpectedly succeeded against a deny-delete handle");
        Require(Bytes(file) == saved, "Failed replacement changed live data");
        Require(Bytes(backup) == savedBackup, "Failed replacement changed the previous backup");
        Require(!fs::exists(temporary), "Failed replacement leaked its temporary file");
        Require(settings::Write(file, first), "Write did not recover after a failed replacement");
        CheckRead(file, first);

        auto destination = second;
        const auto malformed = isolated.path / "malformed.ccpreset";
        Put(malformed, "[combat]\nx=NaN");
        Require(!settings::Read(malformed, destination) && Same(destination, second), "Invalid read partially modified active settings");
        Put(malformed, std::string(65537, 'x'));
        Require(!settings::Read(malformed, destination) && Same(destination, second), "Oversized read modified active settings");
        Put(malformed, std::string(1, static_cast<char>(0xff)));
        Require(!settings::Read(malformed, destination) && Same(destination, second), "Invalid UTF-8 modified active settings");
        Require(!settings::Read(isolated.path / "missing.ini", destination) && Same(destination, second), "Missing read modified active settings");

        const auto legacy = isolated.path / "legacy.ini";
        const std::string legacyBytes = "; keep these original user bytes in backup\r\n[general]\r\ntoggle_key=65\r\n[exploration]\r\nx=27\r\nhalf_life=0.2\r\n";
        Put(legacy, legacyBytes);
        CameraConfig migrated{};
        Require(settings::Read(legacy, migrated), "Legacy user settings failed migration");
        Require(migrated.keys[0] == 65 && migrated.menu_key != 65 && migrated.profiles[CC_EXPLORATION].offset[0] == 27,
            "Migration changed an existing user setting");
        Require(settings::Write(legacy, migrated), "Migrated configuration could not be saved");
        auto legacyBackup = legacy; legacyBackup += L".bak";
        Require(Bytes(legacyBackup) == legacyBytes, "Migration backup lost original formatting or settings");
        CheckRead(legacy, migrated);
        std::cout << "Configuration persistence: creation, repeated replacement, exact backups, migration, invalid data, locked target, and existing temporary-file preservation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Configuration persistence test failed: " << error.what() << '\n';
        return 1;
    }
}
