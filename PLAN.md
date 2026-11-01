# spacedock

A classic-style dock for KDE Plasma.

Package and binary: `spacedock`.

A standalone dock for Plasma 6 on Wayland whose look and behavior match the checklist at the end of this file, closely enough that a longtime user of the reference dock wouldn't notice anything off in 10 minutes of normal use. One developer, one or two AI assistants.

**Behavioral reference:** the OS X 10.9 Dock. We study how it behaves and build our own implementation. No Apple code, artwork, fonts or names are used. Branding and descriptions say "classic-style"; the README may credit the inspiration.

## Ground rules

- Plasma 6, Wayland only. No X11.
- Standalone Qt 6 app. Not a Plasma widget, no widget hosting. Keep it small.
- Every timing and curve comes from measuring the reference dock, never from memory.
- Observe only: no decompiling or disassembling the reference system.
- Reference artwork and screen captures are private working material. They never enter the repo and are never published.

## Architecture

Future goal: localize the application and desktop entry so the interface follows the system language, with a sensible fallback for untranslated strings.

**QML front end**
- Shelf renderer: 3D glass shelf with reflections (bottom), flat panel (left/right)
- Layout and magnification: icon sizes, separator, input region
- Animation engine: bounces, poof, drag slides, all curves read from `tuning.json`
- Stacks and menus: fan, grid, list views; context menus

**C++ backend** (all KDE calls behind one class)
- Task model: libtaskmanager for apps and windows
- Launch and pins: KService, KIO jobs, KConfig pin list
- Files and Trash: KIO, thumbnails, Solid for eject
- Badges: LauncherEntry D-Bus API

**KWin provides**
- Layer shell (via LayerShellQt): placement and layer
- Window protocol: minimize targets, PipeWire window thumbnails
- Screen edges: auto-hide reveal
- Effects: Magic Lamp for minimize, tuned or replaced later

**Design rules**
- The layer-shell surface stays at maximum magnified size. Only content and input region change.
- `tuning.json` is hot-reloaded so tuning needs no rebuild.
- Animations run on elapsed time, not frames (60 and 120 Hz must match).
- Assets load from a theme path: local reference folder in dev, `assets/` in builds.

## Reference workflow

1. Use original hardware running the reference system (VMs skew timing), with a trackpad from the same era.
2. Record each behavior at 120+ fps with a capture card or high-speed camera.
3. Step through frames to extract sizes, curves and durations. Note the reference system's dock preference values.
4. Put every number in `tuning.json` with a comment on how it was measured.
5. Compare with a side-by-side or 50% overlay script, synced to the trigger frame.

Recordings stay on your machine (see Ground rules).

## Assets

- Reference artwork lives outside the repo (e.g. `~/dock-reference/`).
- `assets/MANIFEST.csv` lists every shipped file and its origin.
- CI fails if any committed file matches a hash from the reference folder.
- Original assets to create: shelf and side panel (procedural), indicator light, separator, tooltip bubble (procedural), poof frames, Trash and Eject icons. App and folder icons always come from the user's icon theme.
- The reference UI font is proprietary: ship a metrically similar free font, and let users point at one they own.

## Build order

Each step should leave a dock you can daily-drive.

1. **Setup:** repo, CI asset check, comparison script, reference clips for the checklist below.
2. **Skeleton:** layer-shell window, task model, pinned and running icons, launch/activate, indicators, placeholder shelf.
3. **Shelf and magnification:** glass shelf, reflections, separator, sizing, magnification with correct input region, 2D side style.
4. **Animations and auto-hide:** bounces, tooltips, pressed state, auto-hide via screen edges.
5. **Drag and drop:** reorder with gap and external app drops to pin (offscreen implementation; live pin creation reported broken), drag off with poof, drop onto apps and folders.
6. **Menus and separator:** styled app menus with KDE desktop actions/recent files and Options (implemented; live validation pending), modifier variants and window lists, separator menu, resize, Shift-drag to move edge, keyboard nav.
7. **Stacks and Trash:** fan/grid/list, Automatic view, sorting, spring-loading, Downloads bounce, Trash states, Eject.
8. **Minimized windows and displays:** thumbnails, minimize into app icon, minimize targets for KWin, moving between displays.
9. **Genie (optional):** tuned Magic Lamp, or a custom KWin effect if it doesn't pass.
10. **Polish:** full comparison pass; fix whatever a longtime user of the reference dock says feels off.

## Before a public release

- [ ] Confirm no reference artwork, captures or fonts are in the repo or release artifacts
- [ ] Check for active design patents covering dock appearance from around 2013; get a quick legal read (e.g. Software Freedom Conservancy) if anything turns up
- [ ] Check name conflicts: GitHub, Flathub, distro packages, trademark search

## Known risks

- Resizing the surface during magnification janks → fixed-size surface.
- Wrong input region steals clicks → recompute from drawn icons each frame.
- Hidden dock can't get pointer → KWin screen edges.
- Plasma API churn → one backend class, pin a minimum Plasma version.
- Wayland drag and drop varies by app → test Dolphin, Firefox and a GTK app early.

## Open decisions

- [ ] Minimum Plasma version
- [ ] Start fresh, or borrow integration code from a Plasma 6 Latte port (Lattecotta, Latte Dock NG)
- [ ] Confirm third-party dependency and asset licensing for distribution (project source uses MIT)
- [ ] Reflections: mirrored layer vs. shader
- [ ] Poof: frame art vs. particles
- [ ] Default font

## Behavior checklist

Verify each item on the reference system before building it. Modifier keys: Alt = Option, Meta = Command.

**Look**
- [ ] Perspective glass shelf with icon reflections (bottom)
- [ ] Flat translucent panel (left/right)
- [ ] Glowing indicator under running apps, and the option to hide it
- [ ] Separator before stacks, minimized windows and Trash
- [ ] Icon darkens while pressed
- [ ] Dark rounded tooltip above hovered icon

**Motion**
- [ ] Magnification falloff, follow speed, dock widening
- [ ] Launch bounce repeats until the app finishes launching
- [ ] Attention bounce: once for informational, continuous for critical
- [ ] Auto-hide delay and slide; Alt+Meta+D toggles hiding (opt-in delay/slide and layer-shell input-strip reveal implemented; native pressure reveal and global shortcut remain)
- [ ] Icons slide apart while dragging (implemented provisionally; reference/live validation pending)
- [ ] Poof when dragging an icon off

**Interaction**
- [ ] Click-and-hold or right-click opens the menu; Alt turns Quit into Force Quit
- [ ] Alt-click switches app and hides the current one
- [ ] Meta-click shows the app in the file manager
- [ ] Dropping a file on an app opens it; highlight only if it can open that file
- [ ] Dragged file hovering a folder or stack springs it open
- [ ] Drag separator to resize; Shift-drag to move to another edge
- [ ] Separator menu: hiding, magnification, position, minimize effect
- [ ] Keyboard focus into the dock, arrow-key navigation

**Stacks and Trash**
- [ ] Fan, grid, list; Automatic chooses by item count
- [ ] Grid scrolls when full; folder vs. stack icon; sort options
- [ ] "Open in file manager" item at the end
- [ ] Downloads stack bounces when a download finishes
- [ ] Trash empty/full; becomes Eject while dragging a volume

**Windows and displays**
- [ ] Minimized windows as thumbnails right of the separator, or into the app icon
- [ ] Minimize and restore land exactly on the dock target
- [ ] Dock moves to a display when the cursor pushes its bottom edge
