#include "../plugin/hook_validation.h"
#include <cassert>
#include <limits>

namespace {
__declspec(noinline) int ChainTarget() { return 17; }
__declspec(noinline) int ReplacementTarget() { return 29; }

struct Pages {
    unsigned char* data;
    std::size_t page;
    Pages() {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page = info.dwPageSize;
        data = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 2,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        assert(data);
    }
    ~Pages() { VirtualFree(data, 0, MEM_RELEASE); }
    std::uintptr_t Address(std::size_t offset = 0) const {
        return reinterpret_cast<std::uintptr_t>(data + offset);
    }
    void Protect(std::size_t offset, DWORD access) {
        DWORD before;
        assert(VirtualProtect(data + offset, page, access, &before));
    }
};

void RelativeBranch(std::uintptr_t target, int direction) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    const auto aligned = target & ~(granularity - 1);
    unsigned char* allocation = nullptr;
    // Reserve our own test page near the executable to exercise signed rel32 in
    // each direction; no loaded image, vtable or game process is ever modified.
    for (std::uintptr_t distance = 0x1000000; distance < 0x20000000 && !allocation;
            distance += granularity) {
        const auto address = direction > 0 ? aligned + distance : aligned - distance;
        allocation = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(address),
            info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    }
    assert(allocation);
    const auto address = reinterpret_cast<std::uintptr_t>(allocation);
    const auto displacement = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(address + 5);
    assert(displacement >= (std::numeric_limits<std::int32_t>::min)()
        && displacement <= (std::numeric_limits<std::int32_t>::max)());
    allocation[0] = 0xE9;
    const auto rel32 = static_cast<std::int32_t>(displacement);
    std::memcpy(allocation + 1, &rel32, sizeof(rel32));
    DWORD before;
    assert(VirtualProtect(allocation, info.dwPageSize, PAGE_EXECUTE_READ, &before));
    assert(hooks::Entry(runtime::Entry{"Synthetic", 0, {}}, address));

    // E8 operands may point forward/backward and may already call a different
    // module function. Never replace that original destination with the native
    // metadata target while installing our own chain.
    assert(VirtualProtect(allocation, info.dwPageSize, PAGE_READWRITE, &before));
    auto call = runtime::bodySceneCall;
    call.rva = address + 32;
    call.target = target;
    std::memcpy(allocation + 32 - call.before.size(), call.before.data(), call.before.size());
    std::memcpy(allocation + 37, call.after.data(), call.after.size());
    allocation[32] = 0xE8;
    const auto operand = static_cast<std::int32_t>(static_cast<std::int64_t>(target)
        - static_cast<std::int64_t>(address + 37));
    std::memcpy(allocation + 33, &operand, sizeof(operand));
    assert(VirtualProtect(allocation, info.dwPageSize, PAGE_EXECUTE_READ, &before));
    std::uintptr_t original = 0;
    assert(hooks::SceneCallTarget(call, 0, original) && original == target);

    assert(VirtualProtect(allocation, info.dwPageSize, PAGE_READWRITE, &before));
    const auto replacement = reinterpret_cast<std::uintptr_t>(&ReplacementTarget);
    const auto replacementOperand = static_cast<std::int32_t>(static_cast<std::int64_t>(replacement)
        - static_cast<std::int64_t>(address + 37));
    std::memcpy(allocation + 33, &replacementOperand, sizeof(replacementOperand));
    assert(VirtualProtect(allocation, info.dwPageSize, PAGE_EXECUTE_READ, &before));
    assert(ReplacementTarget() == 29 && replacement != target);
    assert(hooks::SceneCallTarget(call, 0, original) && original == replacement);
    VirtualFree(allocation, 0, MEM_RELEASE);
}

void SceneCalls() {
    Pages pages;
    pages.Protect(0, PAGE_EXECUTE_READWRITE);
    auto call = runtime::bodySceneCall;
    call.rva = pages.Address(64);
    call.target = reinterpret_cast<std::uintptr_t>(&ChainTarget);
    std::memcpy(pages.data + 64 - call.before.size(), call.before.data(), call.before.size());
    std::memcpy(pages.data + 69, call.after.data(), call.after.size());
    auto relative = [&](std::size_t from, std::size_t to, unsigned char opcode = 0xE9) {
        pages.data[from] = opcode;
        const auto delta = static_cast<std::int32_t>(to) - static_cast<std::int32_t>(from + 5);
        std::memcpy(pages.data + from + 1, &delta, sizeof(delta));
    };
    auto indirect = [&](std::size_t from, std::uintptr_t destination) {
        pages.data[from] = 0xFF;
        pages.data[from + 1] = 0x25;
        const std::int32_t delta = 0;
        std::memcpy(pages.data + from + 2, &delta, sizeof(delta));
        std::memcpy(pages.data + from + 6, &destination, sizeof(destination));
    };
    auto rejected = [&](const runtime::SceneCall& spec, std::uintptr_t base = 0) {
        std::uintptr_t unchanged = 0x1234;
        assert(!hooks::SceneCallTarget(spec, base, unchanged));
        assert(unchanged == 0x1234);
    };

    // An existing call through a known anonymous relay keeps that exact relay,
    // while unknown private code is never treated as an executable mod function.
    relative(64, 128, 0xE8);
    indirect(128, call.target);
    std::uintptr_t original = 0;
    assert(hooks::SceneCallTarget(call, 0, original) && original == pages.Address(128));
    auto based = call;
    based.rva -= pages.Address();
    based.target = 0;  // Native target is separately entry-guarded by integration.
    assert(hooks::SceneCallTarget(based, pages.Address(), original));
    assert(original == pages.Address(128));

    // The call's surrounding instruction sequence is immutable; only its rel32
    // operand may differ. Each refusal leaves the caller's chain pointer intact.
    pages.data[64] = 0xE9;
    rejected(call);
    pages.data[64] = 0xE8;
    pages.data[50] ^= 1;
    rejected(call);
    pages.data[50] ^= 1;
    pages.data[80] ^= 1;
    rejected(call);
    pages.data[80] ^= 1;
    pages.Protect(0, PAGE_READWRITE);
    rejected(call);
    pages.Protect(0, PAGE_EXECUTE_READWRITE);

    relative(128, 256);
    relative(256, 384);
    relative(384, 512);
    indirect(512, call.target);
    assert(hooks::SceneCallTarget(call, 0, original) && original == pages.Address(128));
    relative(512, 640);
    indirect(640, call.target);
    rejected(call);
    relative(256, 128);
    rejected(call);
    indirect(128, pages.Address(128));
    rejected(call);
    indirect(128, pages.Address(pages.page));  // Committed data, not executable.
    rejected(call);
    relative(64, pages.page, 0xE8);
    rejected(call);
    relative(64, 128, 0xE8);
    pages.data[128] = 0x48;  // Unsupported mov rax, immediate; jmp rax.
    pages.data[129] = 0xB8;
    std::memcpy(pages.data + 130, &call.target, sizeof(call.target));
    pages.data[138] = 0xFF;
    pages.data[139] = 0xE0;
    rejected(call);

    // Truncated context, relay instruction or indirect pointer must not cause a
    // read into the no-access page, including the bytes after the E8 itself.
    pages.Protect(pages.page, PAGE_NOACCESS);
    auto truncated = call;
    truncated.rva = pages.Address(pages.page - 5);
    rejected(truncated);
    relative(64, pages.page - 1, 0xE8);
    pages.data[pages.page - 1] = 0xFF;
    rejected(call);
    relative(64, 128, 0xE8);
    indirect(128, call.target);
    const auto crossing = static_cast<std::int32_t>(pages.page - 4 - 134);
    std::memcpy(pages.data + 130, &crossing, sizeof(crossing));
    rejected(call);

    auto invalid = call;
    invalid.rva = 13;
    rejected(invalid, pages.Address());
    invalid.rva = UINTPTR_MAX;
    rejected(invalid, 1);
    invalid.rva = call.rva;
    invalid.target = UINTPTR_MAX;
    rejected(invalid, 1);
    invalid = call;
    invalid.rva = UINTPTR_MAX - 10;
    rejected(invalid);
    std::uintptr_t unchanged = 0x1234;
    assert(!hooks::Relative(UINTPTR_MAX - 3, 5, 0, unchanged));
    assert(!hooks::Relative(UINTPTR_MAX - 10, 5, 20, unchanged));
    assert(!hooks::Relative(1, 5, INT32_MIN, unchanged));
    assert(unchanged == 0x1234);
}
}

int main() {
    Pages pages;
    const auto address = pages.Address();
    const auto target = reinterpret_cast<std::uintptr_t>(&ChainTarget);
    assert(ChainTarget() == 17 && hooks::ModuleCode(target));
    assert(hooks::Memory(address, pages.page, false));
    assert(!hooks::Memory(address, 1, true));
    assert(!hooks::Memory(0, 1, false));
    assert(!hooks::Memory(address, 0, false));
    assert(!hooks::Memory(address, (std::numeric_limits<std::size_t>::max)(), false));

    pages.Protect(pages.page, PAGE_NOACCESS);
    assert(!hooks::Memory(pages.Address(pages.page - 4), 8, false));
    assert(!hooks::Memory(pages.Address(pages.page), 1, false));
    pages.Protect(pages.page, PAGE_READWRITE | PAGE_GUARD);
    assert(!hooks::Memory(pages.Address(pages.page), 1, false));
    pages.Protect(pages.page, PAGE_NOACCESS);

    auto expected = runtime::firstUpdate;
    expected.rva = 0;
    std::memcpy(pages.data, expected.prefix.data(), expected.prefix.size());
    assert(!hooks::Entry(expected, address));
    pages.Protect(0, PAGE_EXECUTE_READWRITE);
    assert(hooks::Entry(expected, address));
    assert(!hooks::ModuleCode(address));

    // Indirect RIP-relative branch into a real executable module.
    pages.data[0] = 0xFF;
    pages.data[1] = 0x25;
    std::int32_t displacement = 64 - 6;
    std::memcpy(pages.data + 2, &displacement, sizeof(displacement));
    std::memcpy(pages.data + 64, &target, sizeof(target));
    assert(hooks::Entry(expected, address));

    // A recognized encoding alone does not make a data/anonymous/self target safe.
    const auto dataTarget = pages.Address(128);
    std::memcpy(pages.data + 64, &dataTarget, sizeof(dataTarget));
    assert(!hooks::Entry(expected, address));
    std::memcpy(pages.data + 64, &address, sizeof(address));
    assert(!hooks::Entry(expected, address));
    displacement = static_cast<std::int32_t>(pages.page - 4 - 6);
    std::memcpy(pages.data + 2, &displacement, sizeof(displacement));
    assert(!hooks::Entry(expected, address));

    // A different jump encoding is intentionally unsupported, even if its
    // immediate contains a legitimate target. Do not guess an existing patch.
    pages.data[0] = 0x48;
    pages.data[1] = 0xB8;
    std::memcpy(pages.data + 2, &target, sizeof(target));
    pages.data[10] = 0xFF;
    pages.data[11] = 0xE0;
    assert(!hooks::Entry(expected, address));
    assert(!hooks::Entry(expected, pages.Address(pages.page - 8)));

    // Regression: MinHook's entry E9 commonly targets a private FF25 relay,
    // whose pointer leads to the actual hook in another executable module.
    auto relative = [&](std::size_t from, std::size_t to) {
        pages.data[from] = 0xE9;
        const auto delta = static_cast<std::int32_t>(to) - static_cast<std::int32_t>(from + 5);
        std::memcpy(pages.data + from + 1, &delta, sizeof(delta));
    };
    auto indirect = [&](std::size_t from, std::uintptr_t destination) {
        pages.data[from] = 0xFF;
        pages.data[from + 1] = 0x25;
        const std::int32_t delta = 0;
        std::memcpy(pages.data + from + 2, &delta, sizeof(delta));
        std::memcpy(pages.data + from + 6, &destination, sizeof(destination));
    };
    relative(0, 128);
    indirect(128, target);
    assert(!hooks::ModuleCode(pages.Address(128)));
    assert(hooks::Entry(expected, address));

    // The maximum supported chain contains four recognized jumps.
    relative(0, 128);
    relative(128, 256);
    relative(256, 384);
    indirect(384, target);
    assert(hooks::Entry(expected, address));
    relative(384, 512);
    indirect(512, target);
    assert(!hooks::Entry(expected, address));

    // Multiple anonymous relays cannot conceal a cycle or an unknown stub.
    relative(0, 128);
    indirect(128, pages.Address(256));
    relative(256, 128);
    assert(!hooks::Entry(expected, address));
    indirect(128, pages.Address(128));
    assert(!hooks::Entry(expected, address));
    pages.data[128] = 0x48;
    pages.data[129] = 0xB8;
    std::memcpy(pages.data + 130, &target, sizeof(target));
    pages.data[138] = 0xFF;
    pages.data[139] = 0xE0;
    assert(!hooks::Entry(expected, address));

    // Validate relay bytes and indirect-pointer reads before touching them.
    relative(0, pages.page - 1);
    pages.data[pages.page - 1] = 0xFF;
    assert(!hooks::Entry(expected, address));
    relative(0, pages.page);
    assert(!hooks::Entry(expected, address));
    relative(0, 128);
    indirect(128, target);
    const auto crossing = static_cast<std::int32_t>(pages.page - 4 - 134);
    std::memcpy(pages.data + 130, &crossing, sizeof(crossing));
    assert(!hooks::Entry(expected, address));

    RelativeBranch(target, -1);
    RelativeBranch(target, 1);
    SceneCalls();
}
