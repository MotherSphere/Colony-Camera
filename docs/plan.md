# Colony Camera implementation plan

**Goal:** build an independent, compilable smooth-camera alpha.
**Architecture:** pure Rust core, C++ CommonLibSSE-NG bridge, final native collision.
**Tech stack:** Cargo, C++23, CMake, clang-cl/MSVC.
**Specification:** [design.md](design.md), approved during development.

- [x] Write convergence, frame-rate, invalid-input and teleport tests in `tests/camera.rs`; observe failures with `cargo test`.
- [x] Implement `src/lib.rs`: interpolation, transforms, bounds and strict configuration; run `cargo test` and `clippy -D warnings`.
- [x] Write `plugin/main.cpp` and `plugin/core.h`: C ABI, chained Update/Begin/End hooks, final collision, profiles, shortcuts and rejection of unknown runtimes.
- [x] Build with pinned CommonLibSSE-NG and dependencies under their respective licenses; use CMake source builds and Cargo for Windows.
- [x] Verify the ABI, PE exports, hook points in the local `SkyrimSE.exe` and packaging; record results in `docs/verification.md`.
- [x] Commit coherent steps, prepare an alpha archive and document remaining in-game tests. The initial plan did not authorize automatic installation or pushing; later installations and public publication were separately requested.
