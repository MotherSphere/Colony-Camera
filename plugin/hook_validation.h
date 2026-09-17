#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include "runtime.h"

namespace hooks {
inline bool Memory(std::uintptr_t address, std::size_t size, bool executable) {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !size || !VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region))) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (address < start || address - start > region.RegionSize || size > region.RegionSize - (address - start)) return false;
    if (region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const DWORD code = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (region.Protect & (executable ? code : readable)) != 0;
}
inline bool ModuleCode(std::uintptr_t address) {
    HMODULE module{};
    return Memory(address, 1, true) && GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module);
}
inline bool Relative(std::uintptr_t address, std::size_t size,
    std::int32_t displacement, std::uintptr_t& result) {
    if (address > UINTPTR_MAX - size) return false;
    const auto next = address + size;
    if (displacement >= 0) {
        const auto offset = static_cast<std::uintptr_t>(displacement);
        if (next > UINTPTR_MAX - offset) return false;
        result = next + offset;
    } else {
        const auto offset = static_cast<std::uintptr_t>(-static_cast<std::int64_t>(displacement));
        if (next < offset) return false;
        result = next - offset;
    }
    return true;
}
// Recognize only complete E9 rel32 and FF25 RIP-relative indirect jumps. A relay
// may live in private executable memory (as with MinHook), but arbitrary code
// there is not trusted: at most four jumps must lead to executable module code.
inline bool JumpTarget(std::uintptr_t address, std::uintptr_t& target) {
    if (!Memory(address, 1, true)) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(address);
    std::int32_t displacement;
    if (bytes[0] == 0xE9) {
        if (!Memory(address, 5, true)) return false;
        std::memcpy(&displacement, bytes + 1, sizeof(displacement));
        return Relative(address, 5, displacement, target);
    }
    if (bytes[0] != 0xFF || !Memory(address, 6, true) || bytes[1] != 0x25) return false;
    std::memcpy(&displacement, bytes + 2, sizeof(displacement));
    std::uintptr_t pointer;
    if (!Relative(address, 6, displacement, pointer) || !Memory(pointer, sizeof(target), false)) return false;
    std::memcpy(&target, reinterpret_cast<const void*>(pointer), sizeof(target));
    return true;
}
inline bool RelayChain(std::uintptr_t address) {
    std::array<std::uintptr_t, 4> visited{};
    auto current = address;
    for (std::size_t hop = 0; hop < visited.size(); ++hop) {
        visited[hop] = current;
        std::uintptr_t target = 0;
        if (!JumpTarget(current, target)) return false;
        for (std::size_t prior = 0; prior <= hop; ++prior)
            if (target == visited[prior]) return false;
        if (ModuleCode(target)) return true;
        current = target;
    }
    return false;
}
// No patch is removed: the plugin retains and calls the original chain entry.
inline bool Entry(const runtime::Entry& entry, std::uintptr_t base) {
    if (entry.rva > UINTPTR_MAX - base) return false;
    const auto address = base + entry.rva;
    if (!Memory(address, entry.prefix.size(), true)) return false;
    if (entry.matches(reinterpret_cast<const void*>(address))) return true;
    return RelayChain(address);
}
// Validate an E8 without changing it. Entry guards for the enclosing native
// function and intended callee belong to the caller. Context is checked around
// the operand so an existing call detour remains chainable. Publish its immediate
// destination, not the terminal relay, and leave output unchanged on failure.
inline bool SceneCallTarget(const runtime::SceneCall& call, std::uintptr_t base,
    std::uintptr_t& target) {
    constexpr std::size_t callSize = 5;
    if (call.rva < call.before.size() || call.rva > UINTPTR_MAX - base
        || call.target > UINTPTR_MAX - base) return false;
    const auto address = base + call.rva;
    if (address > UINTPTR_MAX - callSize - call.after.size()) return false;
    const auto start = address - call.before.size();
    const auto size = call.before.size() + callSize + call.after.size();
    if (!Memory(start, size, true)) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(address);
    if (bytes[0] != 0xE8
        || std::memcmp(reinterpret_cast<const void*>(start), call.before.data(), call.before.size()) != 0
        || std::memcmp(bytes + callSize, call.after.data(), call.after.size()) != 0) return false;
    std::int32_t displacement;
    std::memcpy(&displacement, bytes + 1, sizeof(displacement));
    std::uintptr_t current = 0;
    if (!Relative(address, callSize, displacement, current)) return false;
    // Both the intended native function and a mod's replacement must resolve
    // to module code. Anonymous code is accepted only as a bounded known relay.
    if (!ModuleCode(current) && !RelayChain(current)) return false;
    target = current;
    return true;
}
}
