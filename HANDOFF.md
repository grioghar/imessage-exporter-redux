# HANDOFF — context for continuing this project

Detailed notes so any agent (including a local model run via **ollama**) can pick
up where the last session left off **without re-reading the whole history**.
Keep this file CURRENT: update the "Status" and "In flight / next" sections at
the end of every working session. `CLAUDE.md` is the deeper architecture guide;
this file is the running state + how-to-continue.

## ⏩ LIVE HANDOFF (read me first)

**Context handed off to Ollama at token-low. Current live state:**
- **Released:** through **v0.3.0** (six installers each; Homebrew/Chocolatey at
  v0.3.0). v0.3.0 added inline pictures/movies, Google Drive upload, encrypted
  credential saving + Google client-JSON import, the reliable chat.db Browse
  dialog, and the macOS "Fix Full Disk Access…" helper.
- **In flight:** **v0.4.0** on branch `feat/0.4.0-tabbed-ui` → **PR #33** (open).
  Adds the tabbed UI (Source/Filters/Output/Run) + Preferences pane (⌘,) +
  Pause/Stop export, plus fixes: macOS **HEIC→JPEG transcode** (`sips`) so
  pictures render, copy-attachments default-on/auto for HTML+PDF, link-only
  messages show only the card (no bare URL above it), always-editable date
  pickers, and a `date filter: …` log line. Version already bumped to 0.4.0.
- **Finish the v0.4.0 release (immediate next steps):**
  1. `gh pr checks 33` → when green, `gh pr merge 33 --rebase`. If red: open the
     failing job, fix on the branch, commit, push, re-check until green.
  2. `git checkout main && git pull`; `git tag -a v0.4.0 -m "v0.4.0 — tabbed UI,
     Preferences, Pause/Stop, picture+date fixes"`; `git push origin v0.4.0`
     (triggers `.github/workflows/package.yml` → six installers + the release).
  3. After the package run succeeds: download + `sha256sum` the source tarball
     (`archive/refs/tags/v0.4.0.tar.gz`), `iMessage-Exporter-macOS.dmg`,
     `iMessage-Exporter-Setup.exe`; branch `chore/brew-choco-0.4.0`; update
     `packaging/homebrew/imessage-exporter.rb` (url+sha256),
     `imessage-exporter-app.rb` (version + dmg sha256),
     `packaging/chocolatey/imessage-exporter.nuspec` (version) +
     `tools/chocolateyinstall.ps1` (version + exe checksum64); PR → CI → merge.
  4. Refresh this block + append the session log below.
- **Then stop and summarize** unless grio asked for more. Do **NOT** archive the
  project.

## What this project is
`grioghar/imessage-exporter-redux` — a C++17 tool that exports a macOS Messages
database (`chat.db`) to TXT / JSON / HTML. One shared engine, many front-ends:
CLI, Qt desktop GUI (macOS/Windows/Linux), and an iOS SwiftUI app (via a pure-C
bridge). MIT licensed. Clean-room; shares no code with the Rust
`ReagentX/imessage-exporter`.

## Architecture (the load-bearing idea)
- **`imsg_core`** (static lib, NO SQLite, unit-tested anywhere): `log`, `models`,
  `time_util`, `attributed_body`, `exporters`, `contact_book`, `vcard`.
- **`imsg_db`** (static lib, +SQLite): `database`, `export_job`, `contacts`,
  `backup`. Only this + `main.cpp` touch SQLite.
- **`imsg_mobile`** / `imsg_bridge.cpp`: pure-C ABI (`include/imsg/imsg_bridge.h`)
  for Swift/embedding. SwiftPM `Package.swift` builds the same sources for iOS.
- **GUI** (`gui/`, Qt6 Widgets, optional via `find_package(Qt6)`): `main_window`,
  `icloud_contacts` (CardDAV), `updater` (self-update), `resources.qrc` (icon).
- **Rule:** put parsing/formatting/IO in the engine; keep front-ends thin. Prefer
  the SQLite-free core so logic stays testable. Match the terse house style;
  comments say *why*, not *what*.

## Where the data comes from (and the hard limits — already decided, don't relitigate)
- Messages: a Mac's synced `~/Library/Messages/chat.db` (default), or extracted
  from an **unencrypted** iTunes/Finder backup (`--backup`). **No iCloud pull for
  messages** — no API + end-to-end encrypted. Encrypted-backup decryption is NOT
  implemented (would need a crypto dep + real data).
- Contacts: macOS AddressBook (`.abcddb`), a vCard `.vcf`, the iOS AddressBook
  from a backup, or **iCloud CardDAV via an app-specific password** (GUI button).
  Raw Apple-ID/2FA/SRP login is intentionally NOT built.
- Ephemeral virtualized-macOS puller (UTM/QEMU): **rejected** — running macOS on
  non-Apple hardware violates Apple's SLA. (Cloud Apple hardware would be the
  only license-clean route; not built.)

## Build / test / validate
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build   # core tests
cmake --build build --target imessage-exporter        # CLI (needs sqlite)
cmake --build build --target imessage-exporter-gui    # GUI (needs Qt6)
```
- **No local C++ toolchain in the dev sandbox** — CI is the source of truth.
  Validate by pushing a branch/PR and reading GitHub Actions.
- Tests are a hand-rolled harness in `tests/test_core.cpp` (no framework), core
  only. Add assertions there for any new core logic.

## CI / workflows (GitHub Actions)
- `ci.yml`: `build-test` (ubuntu+macos engine/CLI), `gui` matrix
  (ubuntu/macos/windows via `jurplel/install-qt-action`; Windows SQLite via
  vcpkg), `docker`.
- `package.yml` (manual dispatch or `v*` tag): builds installers — macOS `.dmg`,
  Windows Inno `.exe`, Linux `.AppImage` + `.deb` + `.rpm` + Snap — and a
  `release` job publishes them on a real `v*` tag (skips tags containing `test`).
- `docker-publish.yml`: pushes the CLI image to GHCR (and Docker Hub, enabled).
- **Validation trick:** push a throwaway tag `vX-test...` to exercise
  `package.yml` without creating a release; delete the tag after.

## Conventions / gotchas learned the hard way
- Commit msgs: bash here-strings break on apostrophes — write the message to a
  file and `git commit -F`, or avoid `'` in `-m`.
- Don't push straight to `main` (auto-mode blocks it). Land via PRs. The PR stack
  was merged by fast-forwarding earlier; rebase-merging a stack rewrites SHAs and
  causes conflicts — rebase the tip onto updated `main` and merge once.
- MSVC: use `/W3`, not `-Wall -Wextra` (guarded in CMake).
- Windows GUI must be `WIN32_EXECUTABLE` (no console window) + SQLite via vcpkg.
- `CMAKE_POSITION_INDEPENDENT_CODE ON` so static libs link into the Qt PIE exe.
- AppImage: name the output via `OUTPUT` env so `mv *.AppImage` doesn't grab the
  linuxdeploy tools.
- Version lives in `include/imsg/version.hpp` (`IMSG_VERSION`). Keep it in sync
  with CMake `project(VERSION)`, `packaging/windows/installer.iss`,
  `man/imessage-exporter.1`, `snap/snapcraft.yaml`, and the Homebrew/Choco defs.

## Status (UPDATE THIS EACH SESSION)
- **v0.3.0 IN PROGRESS** (branch `feat/0.3.0-media-drive`): big batch.
  1. **Inline media fix** — the HTML/Markdown renderers keyed inline `<img>`/
     `<video>` off `attachment.mime_type`, which the Messages DB often leaves
     empty, so pictures/movies degraded to bare links. Added `effective_mime()`
     (guesses from file/transfer/copied-path extension; shared `mime_from_ext`)
     in exporters.cpp; images now `<img loading="lazy">` linking to the copy,
     movies `<video controls preload="none">` with a fallback link, Markdown
     `![](path)`. Test `test_inline_media_fallback`.
  2. **PDF in place** — `startExportResuming` forces copy (if not embedding) for
     PDF; `exportFinished` sets `QTextDocument::setBaseUrl` to the temp HTML dir
     so relative copied images embed into the PDF. (HEIC won't decode in
     QTextDocument — JPEG/PNG/GIF do; note as a caveat.)
  3. **Lazy hero cards** — `loading="lazy"` on og-card + inline imgs (native, so
     offline-safe; PDF renders all in place). True client-side AJAX is impossible
     for a file:// doc, so lazy-load is the offline-correct interpretation.
  4. **Encrypted creds + save checkbox** — `gui/google_auth.{hpp,cpp}` centralizes
     client id/secret in the keychain (migrated off plaintext QSettings) + a
     `parseClientJson`. iCloud and Google dialogs prefill from the keychain and
     have a "Save credentials (encrypted)" checkbox; `GoogleContacts::setClient`
     lets a run use unsaved creds. iCloud Apple ID + app pw now stored encrypted.
  5. **Google client JSON import** — "Import client JSON…" button parses the
     downloaded `client_secret_*.json` (installed/web). Docs + Help updated.
  6. **Google Drive connector** — `gui/google_drive.{hpp,cpp}`: OAuth PKCE
     (`drive.file` scope), refresh token persisted in keychain (always, so uploads
     survive restart); synchronous recursive uploader (`drive::uploadDirectory`,
     own QNAM + QEventLoop per request, runs on a QtConcurrent worker) creates the
     named folder under root and recreates the subfolder tree. GUI: "Connect
     Google Drive…", folder-name field, "Upload export to Drive when finished"
     (persisted `ui/uploadDrive`,`ui/driveFolder`); fires in `exportFinished` via
     `maybeUploadToDrive`.
  Version bumped to 0.3.0. NEXT: PR → CI green → merge → tag v0.3.0 → brew/choco.
- Released **v0.2.8** (brew/choco at v0.2.8; six installers): **true Open Graph
  rich link previews**. Design: the core renderer stays network-free; it exposes
  an optional resolver hook `imsg::set_link_preview_resolver(LinkPreviewFn)`
  (exporters.hpp) that `media_embeds_html` calls for each non-embeddable URL — a
  non-empty return is used verbatim, empty/none falls back to the offline
  favicon `link_card`. The GUI supplies the fetcher in `gui/link_preview.cpp`
  (`linkpreview::fetch_og_card`): a **synchronous** Qt-Network GET (own
  QEventLoop, 6 s timeout, size caps) that parses `og:`/`twitter:` meta (+`<title>`
  fallback), fetches the hero image and embeds it as a base64 data URI (so the
  export stays self-contained offline), and returns an `.ogcard` HTML block
  (CSS added to `kHtmlStyle`). Runs on the export worker thread; results cached
  (QHash+QMutex, negatives too). Opt-in GUI checkbox "Rich link previews
  (online)" (member `richPreviews_`, persisted `ui/richPreviews`); set in
  `startExportResuming`, cleared in `exportFinished`. CLI doesn't set a resolver
  → still gets favicon cards. Core test `test_link_preview_resolver` covers the
  hook (no network). Only HTML/PDF use it. Version bumped to 0.2.8
  (version.hpp/CMake/installer.iss/man/snapcraft); brew/choco refreshed
  post-release as usual.
- Released **v0.2.7** (brew/choco at v0.2.7; six installers): Select-People
  picker maps contacts↔numbers ("Name — +1…"), substring-both-ways participant
  match; **From/To fix** (changing a date now ticks its filter; From-only =
  through today); per-conversation attachment **folder** `Name/` alongside
  `Name.<ext>` with a "Hidden attachments folder" option
  (`opts.hidden_attachment_dir`, leading-dot); **favicon link cards** for
  non-embeddable URLs (`link_card`/`host_of` in exporters; `.linkcard` CSS) —
  now superseded by the opt-in OG cards above.
- Earlier: **v0.2.4** Markdown (engine) + PDF (GUI, QTextDocument→QPdfWriter);
  ad-hoc-codesigned DMG w/ two install targets; "Copy Messages data" button.
  **v0.2.3** persistent `ContactStore` (SQLite), Google Contacts OAuth-PKCE +
  People API, OAuth token in OS keychain (`secret_store`), GUI settings persist,
  job resume/recovery, people picker. **v0.2.1** Unicode filenames, FDA
  guidance, rich error dialog. v0.2.0/v0.1.0 base.
- `main` (@ bc37a6e before this branch): everything merged — engine + backup +
  logging + Docker, Qt GUI (Help menu, iCloud CardDAV, Google Contacts,
  auto-update), all six installer pipelines, Homebrew + Chocolatey at v0.2.7.
- **Open PRs:** the `feat/0.2.8-og-previews` PR (this work).

## In flight / next
- Publish to the actual registries (one-time, needs accounts): create
  `grioghar/homebrew-tap` and copy the two `.rb` files; `choco pack` +
  `choco push` with a community API key; GHCR/Docker Hub already automated.
- For the NEXT release, bump `IMSG_VERSION` + CMake `project(VERSION)` +
  installer.iss + man page + snapcraft.yaml, tag `vX.Y.Z`, then re-fill the
  Homebrew/Choco checksums against the new assets (see "Build/validate").
- iOS app is source-only (needs Xcode + Apple Developer acct; not CI-built).
- Installers are UNSIGNED (macOS notarization / Windows cert need credentials).
- Still unbuilt by design: encrypted-backup decryption; attachment extraction
  from backups; ephemeral cloud-Mac puller.
