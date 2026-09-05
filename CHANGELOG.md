# Changelog

## 1.2.0 — 2026-09-05

- Stop Import from flickering on resize after Open ZIP.
- Warn when Steam is open during Remove: psyche only edits config.yaml, so restart Steam to refresh the library.
- Say when a Hubcap key is expired, and show quota/expiry after a 401/403/429.

## 1.1.0 — 2026-09-04

- Stop writing depot-only IDs into AdditionalApps. Keyed IDs are added there only when steamcmd app info says they are apps.

## 1.0 — 2026-09-04

- Confirm Hubcap downloads before they use quota, and explain when a key is missing.
- Tabs are now Games, Import, Library, History, Settings, with a next step into Library after apply.
- Drop a ZIP, pick a destination, close while busy, and compact widths behave more clearly.
- Settings shows the version; dialogs and lists match the dark chrome.

## 0.3 — 2026-09-04

First public release.

- Search Hubcap and import packages by AppID (full Lua, base game, DLC, or ZIP) or a local ZIP.
- Merge recognized apps, depots, and keys into SLSsteam `config.yaml` with backups and History restore.
- Library view can remove entries psyche added (or recovered from labelled comments).
- Headless CLI (`--search`, `--appid`, `--apply`, `--restore`, `--health`, `--stats`).
- Bundled Linux x86_64 ZIP with Qt runtime, built on Debian 13.
