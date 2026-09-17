# Camera Colony 0.1.2 alpha — distribution update

This package restores licensing materials and corresponding source access.
There are no changes to the DLL, configuration or camera behavior, and no new
runtime requirements. The plugin version remains 0.1.2.

- `Camera-Colony-0.1.2-alpha-update.zip`: installable mod with SKSE at the archive
  root, GPL license, third-party notices, upstream license files and exceptions,
  Rust runtime notices, exact project/dependency source archives and checksums.
- `Camera-Colony-0.1.2-sources.zip`: the same sources and notices, without the DLL
  or INI. The source-only package is for developers, not installation.

The corresponding source commit is `f3a1e9b0eeaeabaa268d1bd16130d2d40df275d5`
(tag `v0.1.2`). The default branch is the later 0.1.3 diagnostic, not the source
revision used to build this DLL. Historical source archives remain unchanged.

## Verification

Both ZIP files passed CRC and byte-for-byte content checks. DLL and INI match
the original verified 0.1.2 archive; neither was rebuilt. Required notices and
source archives are present. Source tar archives were opened successfully.
No new gameplay testing was needed for this packaging-only change.

SHA256:

```text
65306b624e2ed002f8fd327c0c77a3851e4c17463b89d81fc09c92bb91b7106e  Camera-Colony-0.1.2-alpha-update.zip
5f466e2c421e6d78084b9fad8b4d725ae8a1fa29be7296528735588dfe03a4c8  Camera-Colony-0.1.2-sources.zip
```

## Nexus source link

Keep the following link next to the mod download:

Source code (GPL-3.0-or-later), including the exact 0.1.2 project and dependency
sources: https://github.com/MotherSphere/Colony-Camera/releases/tag/v0.1.2

Preserve the licenses and notices when redistributing. SKSE and Address Library
are runtime requirements; installing them does not replace the distribution
materials included here.
