# Third-party notices

Colony Camera's original code is GPL-3.0-or-later; see LICENSE.
No SmoothCam implementation, presets, UI or assets are included.

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
