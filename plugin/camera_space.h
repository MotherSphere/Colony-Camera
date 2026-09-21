#pragma once
namespace camera {
// Worlds identify continuous exterior space. Interiors are identified by cell.
// Compare stored identities only; the previous cell may already be unloaded.
inline bool SameSpace(const void* previousCell, const void* previousWorld,
    const void* cell, const void* world) {
    return cell && previousCell && previousWorld == world
        && (previousCell == cell || world);
}
}
