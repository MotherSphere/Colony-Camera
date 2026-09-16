# Colony Camera Implementation Plan

**Goal:** produire une alpha indépendante compilable de caméra fluide.
**Architecture:** moteur Rust pur, passerelle C++ CommonLibSSE-NG, collision native finale.
**Tech Stack:** Cargo, C++23, CMake, clang-cl/MSVC.
**Spec:** design.md (architecture approuvée dans la conversation).

- [x] Écrire les tests de convergence, FPS, invalides et téléportation dans tests/camera.rs, constater les échecs avec cargo test.
- [x] Implémenter src/lib.rs : interpolation, transformations, bornes et configuration stricte ; cargo test et clippy -D warnings.
- [x] Écrire plugin/main.cpp et plugin/core.h : ABI C, hooks Update/Begin/End chaînés, collision finale, profils, raccourcis, refus runtime inconnu.
- [x] Construire avec CommonLibSSE-NG épinglé et dépendances sous leur licence ; CMake source + Cargo Windows.
- [x] Vérifier ABI, exports PE, points d'accroche du SkyrimSE.exe local et packaging ; conserver les résultats dans docs/verification.md.
- [x] Committer chaque étape cohérente, préparer une archive alpha et documenter les tests en jeu restant à effectuer. Pas de push ni d'installation automatique.
