# Third-party notices

Colony Camera's original code is GPL-3.0-or-later; see LICENSE.
No SmoothCam implementation, presets, UI or assets are included.

The no-headbob body-placement algorithm in src/first_person.rs is adapted from
ImprovedCameraSE-NG, ArranzCNL and contributors, revision
2e441c190e46d96eefb7738a3276308e9c36e939,
ImprovedCamera/source/skyrimse/ImprovedCameraSE.cpp (AdjustModelPosition and
TranslateThirdPersonModel). Source: https://github.com/ArranzCNL/ImprovedCameraSE-NG/tree/2e441c190e46d96eefb7738a3276308e9c36e939
The adapted file retains MPL-2.0 and is additionally distributed under
GPL-3.0-or-later in this combined work pursuant to MPL section 3.3.
See licenses/ImprovedCamera/MPL-2.0.txt. Modified September 19, 2026: Rust ABI,
input validation, bounded displacement and parent-transform handling. This is
a scoped ordinary-first-person adaptation, not full Improved Camera parity.


- CommonLibSSE-NG / alandtse CommonLibVR, commit c7662fc59e531f8c14d00665c5ad15a0577dc9b6.
  https://github.com/alandtse/CommonLibVR
  GPL-3.0-or-later and the notices/exceptions in its COPYING.txt, EXCEPTIONS.md and licenses/.
- spdlog, commit 486b55554f11c9cccc913e11a87085b2a91f706f.
  https://github.com/gabime/spdlog ; MIT (including its applicable bundled notices).
- Microsoft DirectXMath, commit d837578297c6c93849573858182350ede04987dc.
  https://github.com/microsoft/DirectXMath ; MIT.
- Microsoft DirectXTK, commit 642825891c41b1e7e4d6f934171f45b2645b713e.
  https://github.com/microsoft/DirectXTK ; MIT. Header types used by CommonLib.

The package includes the upstream license files without replacing their credits.
The Rust standard library is distributed under MIT OR Apache-2.0; the install package
includes its complete COPYRIGHT-library.html notice, preserved inside LICENSES.txt. No new crates are needed by the Rust camera core.

Dependency checkouts are fetched at the exact revisions in dependencies.json.
The separately downloadable source archive contains project source, build instructions and this manifest;
the accompanying dependency archive supplies the referenced dependency sources.

SKSE Menu Framework 3 API header: QTR-Modding, revision
1dcb70179076aae4ab626f43c5baab2735ca5877, LGPL-2.1.
https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API
The unmodified header and complete license are in third_party/skse-menu-framework.
The framework DLL is optional, separately installed, and is not bundled.
