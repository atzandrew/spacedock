# Code map

Where each part of the dock lives. Start with "I want to change..." below; the
file index after it lists every source file. Keep this current when files are
added, split or renamed. Line numbers are deliberately omitted (they go stale);
search for the named property, function or `objectName`.

## I want to change...

| What | Where |
|---|---|
| Any animation speed or delay | `tuning.json` (hot-reloaded; see "Tuning" below) |
| Icon size, spacing, shelf geometry, reflection strength | `tuning.json` → `layout` |
| Magnification amount / falloff | `tuning.json` → `magnification.scaleSamples`; math in `qml/Magnification.js` |
| Tooltip look (font, shadow, padding) | `tuning.json` → `tooltip`; drawing in `qml/DockToolTip.qml`, shadow raster in `src/tooltipshadow.cpp` |
| Launch bounce height / curve | `tuning.json` → `feedback`; player in `qml/Bounce.qml` |
| Auto-hide timing and reveal strip | `tuning.json` → `autoHide`; state machine in `qml/AutoHideController.qml` |
| Shelf (glass floor) appearance | `qml/Shelf.qml` |
| Running-app indicator, icon reflection, open fade | `qml/TaskArtwork.qml` |
| Right-click menu panel: shape, border, shadow, arrow, corner radius, padding, width | `qml/DockMenu.qml` (`onPaint`, `pointerHalfWidth`, `cornerRadius`, `pointerTipInset`) |
| Menu row: font, row height, text insets, highlight bar, check mark, submenu ▶ | `qml/DockMenuItem.qml` |
| Menu separators | `qml/DockMenuSeparator.qml` |
| Where the menu opens relative to the icon | `DockMenu.placeAbove()` in `qml/DockMenu.qml` |
| Where submenus open relative to their row (Wayland anchor, `submenuDrop`) | `src/submenuplacement.h`; `submenuDrop` in `qml/DockMenu.qml` |
| Folder grid popup | `qml/FolderView.qml`; folder pinning, contents and asynchronous cached thumbnails in `src/dockbackend.cpp` |
| What's in an app's right-click menu | `qml/TaskMenu.qml` (layout); entries/actions from `DockBackend::contextEntries` / `taskAction` in `src/dockbackend.cpp` |
| AppImage "choose the running app" menu | `qml/AppImageSetupMenu.qml`; logic in `src/appimagecontroller.cpp` |
| Drag-to-reorder behavior | `qml/DragController.qml` |
| Dropping apps from the launcher onto the dock | `qml/ExternalDropController.qml`; validation/pinning in `DockBackend::canPinUrls` / `pinUrls` |
| Click to launch/activate, pin/unpin, close windows | `src/dockbackend.cpp` (`activate`, `togglePin`, `taskAction`) |
| Which clicks/hover reach the dock (input region) | `requestInputUpdate` in `qml/Main.qml` → `DockBackend::setInputRegion` |
| Window/layer-shell setup (screen edge, exclusion zone) | `qml/DockWindow.qml`, `src/startupenvironment.h` |
| Startup/quit animation and signal handling | `src/docklifecycle.cpp`, `beginShutdown` in `qml/Main.qml` |
| Command-line flags and debug aids | `src/main.cpp` |

## Tuning (`tuning.json`)

Edited live: the dock reloads on save. Invalid values are rejected with a
warning and the last good settings are kept. Validation and defaults:
`src/tuning.cpp`. The same file is embedded in the binary as the fallback.

| Section | Contents |
|---|---|
| `layout` | icon size, spacing, padding, surface/shelf geometry, reflections, indicators |
| `magnification` | on/off, scale falloff, enter/exit durations and easing samples |
| `tooltip` | on/off, delay, size, font, shadow |
| `feedback` | launch bounce on/off, height, duration, curve |
| `autoHide` | on/off, hide/reveal delays, slide duration, reveal strip, hidden scale |
| `motion` | menu highlight fade, parting/slide duration, drag parting delay |
| `measurements`, `provenance` | notes on where values came from; not read by the code |

## File index

### QML front end (`qml/`)

| File | Owns |
|---|---|
| `Main.qml` | The dock itself: icon row (`viewport` / `taskDelegate`), magnification state and animation, pointer tracking, menu holds (freezes layout while a menu is open), input region, tooltips, drop areas. Wires the controllers below together. |
| `DockWindow.qml` | Production layer-shell window around `Main`. |
| `PreviewWindow.qml` | Ordinary window around `Main` for `--preview`, tests and captures. |
| `Magnification.js` | Pure layout math: icon positions/sizes for a pointer position, insertion gaps, hover rectangles, eased progress. |
| `Shelf.qml` | Procedural glass shelf. |
| `TaskArtwork.qml` | One icon's artwork, reflection and open fade. |
| `Bounce.qml` | Launch-bounce player (lives on the dock so it survives delegate replacement). |
| `DragController.qml` | Reorder gesture: pickup, gap targeting, landing, native drag handoff past the dock edge. |
| `ExternalDropController.qml` | Drops of apps from outside (launcher, file manager). |
| `AutoHideController.qml` | Auto-hide timing, holds and slide progress. |
| `AppImageArtworkTransition.qml` | Cross-fade when an AppImage's placeholder icon is replaced by its real one. |
| `DockToolTip.qml` | App-name label above the hovered icon. |
| `DockMenu.qml` | Shared menu base: painted panel/arrow, sizing to content, root-menu placement. Used for root menus and submenus. |
| `DockMenuItem.qml` / `DockMenuSeparator.qml` | Menu rows and separators. Hide rows with `shown`, not `visible`. |
| `TaskMenu.qml` | An app's right-click menu and its dock hold. |
| `FolderView.qml` | Themed folder grid popup and its dock hold. |
| `AppImageSetupMenu.qml` | AppImage association prompt. |
| `LaunchTestWindow.qml` | Window of the slow-launch test helper (`src/launchtest.cpp`). |

### C++ backend (`src/`)

| File | Owns |
|---|---|
| `main.cpp` | Startup, command-line flags (`--preview`, `--tuning`, `--trace-*`, `--debug-open-menu`, ...), engine setup. |
| `submenuplacement.h` | Tells KWin where submenus open: grow up from the row's bottom-right corner. Qt ignores `Menu.x/y` on Wayland. |
| `dockbackend.h/.cpp` | The only code that talks to Plasma's private task manager: task model, activate, pin/unpin, reorder, context menu entries, URL drops, native drag, cursor position, input region. |
| `appimagecontroller.h/.cpp` | AppImage setup sessions and live-to-pinned associations (through `AppImageTaskAdapter`). |
| `appimagestore.h/.cpp` | AppImage desktop entries, selection markers and artwork on disk. Never runs AppImages. |
| `tuning.h/.cpp` | Loads, validates and hot-reloads `tuning.json`. |
| `tooltipshadow.h/.cpp` | Cached tooltip shadow images. |
| `docklifecycle.h/.cpp` | Routes quit requests and Unix signals through the exit animation. |
| `startupenvironment.h` | Dock-only Qt platform overrides, restored before launching apps. |
| `launchtest.cpp` | Slow-launch test helper app. |

### Tests (`tests/`, run with `ctest --test-dir build`)

| File | Covers |
|---|---|
| `magnification_test.cpp` | `Magnification.js` math. |
| `magnification_ui_test.cpp` | Live UI: geometry, menus (placement, layout freeze, outside click), lifecycle, drag/drop. |
| `artwork_ui_test.cpp` | Artwork, tooltips, bounce survival across task replacement, shadow cache. |
| `dockbackend_test.cpp` | Backend reorder/pin/desktop-action behavior against the KDE model. |
| `appimage_integration_test.cpp`, `appimagestore_test.cpp` | AppImage flows and storage. |
| `tuning_test.cpp` | Tuning validation, defaults and hot reload. |
| `startupenvironment_test.cpp`, `launcher_trust_test.cpp`, `test_*.py` | Launch environment, desktop registration, lifecycle signals, asset policy. |
| `ui_test_support.h`, `launcher_model.h` | Shared test helpers. |

### Tools and docs

| Path | Purpose |
|---|---|
| `tools/run-dev.sh` | Register the dev build with KDE, then run it. |
| `tools/register_desktop.py` | Desktop-entry registration used by `run-dev.sh`. |
| `tools/check_assets.py`, `tools/reference-sha256.txt` | Keep private reference material out of the repo. |
| `tools/compare_clips.py` | Side-by-side comparison of reference and dock recordings. |
| `PLAN.md` | Product plan, design rules, open decisions. |
| `AGENTS.md` | Agent entry point and project instructions. |
