# spacedock

![SpaceDock logo](assets/images/spacedock.png)

Spacedock is an application built on Qt for KDE's Plasma desktop environment. It is designed as a replacement for Plasma's conventional panel and application launchers and is inspired by classic Mac OS X. It uses no proprietary code.

### Notice

**This is an AI developed project.** Care has been put into its design, functionality, security, and performance—but this is a product of primarily AI code. Use at your own discretion.

This tool is in beta and **will** have bugs! Please report them.

![Spacedock screenshot of an idle dock](assets/images/spacedock_screenshot1.png)

![Spacedock's magnification example](assets/images/spacedock_screenshot2.png)

## Current version Beta 0.7.0 - November 5, 2026

Changes since 0.5.1:

* Folder pinning with fan, list, and grid views, sorting, thumbnails, and drag-out
* System language support with translations for ten additional languages
* Improved hover and magnification performance
* Updated trashcan artwork and menu selection feedback
* Improved application icon picker behavior
* Hover benchmarks and fixes for CI builds and tests

### Features

* A Mac OS X inspired dock with toggleable and tweakable magnification and size
* Toggleable auto-hide with smooth animations
* Drag and drop application and folder shortcuts
* Trashcan built into the dock with drag and drop and empty confirmation
* Touch release activation, delayed labels and magnification, and long-press menus
* Real-time dock scaling
* Icon label and image customization
* AppImage handling/workaround
* Folder fan, list, and grid views with sorting, thumbnails, and drag-out
* Right-click menus for applications and folders, including application actions and recent files
* Localization that follows the system language at startup. Translations have only been validated by AI and have not been reviewed by native speakers; corrections are welcome.

### Touch controls

Lift your finger to activate the item underneath it. Hold for 0.15 seconds to
show its label, or 0.3 seconds to magnify the dock. Slide to browse other icons;
releasing outside the dock cancels. Holding nearly still for 0.75 seconds opens
the item's context menu and consumes the release. After sliding, holding roughly
still for another 0.75 seconds opens the menu for the current item. Movement
beyond the system drag threshold restarts that hold. Touch context menus and their submenus use twice the normal size, with scrolling
for tall menus. Touch does not rearrange icons.

### Languages

SpaceDock uses the system language at startup. It supports English, French,
German, Spanish, Brazilian Portuguese, Italian, Russian, Ukrainian, Polish,
Simplified Chinese, and Japanese. Restart SpaceDock after changing the system
language. Unsupported languages fall back to English.

### Features in development

* Application launcher
* Themes, including flat design
* Better DPI scaling (developed on 1.5x scale; other variations need testing)
* Drag dock separator to resize
* Further right-click menu design improvements
* Better support for older versions of Plasma and Qt
* Left and right dock positions

### Known bugs and limitations

* Some unnecessary image/icon calls during dock scaling
* Some animations have some jank, such as AppImage shuffling when assigning apps
* Fullscreen windows can cover the auto-hide reveal strip
* You tell me! Submit bugs and I can work on fixing them

## Build

SpaceDock requires Plasma 6 on Wayland, Qt 6.6 or newer, KDE Frameworks 6, CMake 3.22 or newer, Qt Linguist tools, and a C++20 compiler.

### Debian 13

From a Plasma Wayland desktop session, run:

```sh
./tools/install-debian.sh
```

The script installs dependencies, builds SpaceDock, and installs it under `~/.local`. Run it as your normal user; it asks for `sudo` when installing packages. The desktop has been tried on Debian 13, but the installer has not yet been verified on a fresh installation.

Start SpaceDock from the application menu or run `spacedock` in a Plasma Wayland session. The command is installed at `~/.local/bin/spacedock`; see [Command-line access](#command-line-access) if your shell cannot find it.

### Fedora

```sh
sudo dnf install gcc-c++ cmake make python3 qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qttools-devel kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kservice-devel kf6-kwidgetsaddons-devel plasma-workspace layer-shell-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./tools/run-dev.sh
```

Run the last command in a Plasma Wayland desktop session. The development runner registers the desktop entry before starting the dock. To run the tests, use `ctest --test-dir build --output-on-failure`.

To install the built application for your user and make it available from the application menu, run:

```sh
cmake --install build --prefix "$HOME/.local"
```

You can then run `spacedock` in a Plasma Wayland session. See below if your shell cannot find the command.

### Arch Linux and Arch-based distributions

Install the dependencies, then build and install SpaceDock for your user:

```sh
sudo pacman -Syu --needed base-devel cmake python \
  qt6-base qt6-declarative qt6-wayland qt6-tools \
  kconfig kcoreaddons kservice kwidgetsaddons \
  plasma-workspace layer-shell-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake --install build --prefix "$HOME/.local"
```

Start SpaceDock from the application menu or run `~/.local/bin/spacedock` in a terminal on the Plasma Wayland desktop. An ordinary SSH shell does not have the graphical session environment needed to start the dock. You can check the desktop session type with `echo "$XDG_SESSION_TYPE"`; it should print `wayland`.

### Updating, login startup, and removal

After updating your source checkout, install or update the application with:

```sh
./tools/install.sh
```

This builds a Release version in `build-install` and replaces the installation
under `~/.local`. Dependencies must already be installed; Debian users can use
`./tools/install-debian.sh` for dependencies and installation together. The
installer installs the current checkout; it does not fetch changes from GitHub.
Use `--prefix PATH` or `--build-dir PATH` to override either location.

Quit the running dock and launch SpaceDock from the application menu to use an
update. Installation preserves preferences, pinned items, and your login startup
choice. In Dock Settings, enable **Start at login** to launch the installed copy
when signing into Plasma. This control is disabled for development and preview
runs. An existing startup entry pointing to another installation must first be
adjusted through Plasma's Autostart settings.

Installed and development docks share a single-instance lock: launching a second
dock exits without opening another one. Preview and automated UI test windows are
independent. An older dock started before this protection was added must be quit
once before using it.

Development runs through `./tools/run-dev.sh` register **SpaceDock (Development)**
as `spacedock-dev.desktop`, leaving the installed `spacedock.desktop` launcher and
autostart entry alone. Quit the installed dock before starting a development dock.
Both use your existing dock preferences. If a previous development run redirected
the installed launcher, running the installer once restores it.

To uninstall the default installation:

```sh
~/.local/bin/spacedock-uninstall
```

Or, from this checkout:

```sh
python3 tools/uninstall.py --prefix "$HOME/.local"
```

Uninstall removes application files and a login entry pointing to that
installation. It retains saved preferences, pins, custom artwork, and development
launchers. Quit any running dock separately. The installed uninstaller requires
Python 3.

### Command-line access

The user install places the executable in `~/.local/bin`. If `command -v spacedock` finds nothing, add that directory to your shell's `PATH`. For Bash, add this line to `~/.bashrc`; for Zsh, add it to `~/.zshrc`:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Open a new terminal for the change to take effect. You can also run `~/.local/bin/spacedock` directly.

## See it in action

### Repeatable hover benchmark

Run `./tools/run-dev.sh --benchmark-hover` with only one dock running. Keep the
pointer away from the dock during the test. Magnification must be enabled and
fit on the display; otherwise the benchmark reports that it cannot run.
After startup, the simulated pointer sweeps across the dock every four seconds.
Following a two-second warmup, the benchmark records twenty seconds and prints
Qt swaps per second and frame-interval median, 95th percentile, maximum, and the
number of intervals exceeding 20 ms. The 60 fps target corresponds to 16.7 ms.
These are Qt swap completions, not compositor presentation measurements. The
dock returns to normal pointer control after the sample.

Use `--benchmark-pointer` instead to measure real mouse movement during the
same twenty-second window, without the simulated sweep. Keep moving across
the icons throughout the recording. This also reports the mouse-event count
and intervals delivered to the Qt window. Results include any idle periods
within the window, so coordinate the start before comparing runs.

For a faster stress test, add `--benchmark-hover-period 250` (four complete
sweeps per second). Add `--benchmark-hover-observations 8` to route eight
interpolated observations per frame through the normal hover-processing
function. This exercises hover logic, but does not inject OS mouse events or
measure hardware input latency.

`tools/replay-pointer.cpp` provides a Linux-native virtual mouse for testing the
OS input path as well. Build it separately with a C++20 compiler and run it with
access to `/dev/uinput`. Its arguments are event rate, cycle period in seconds,
horizontal span in relative counts, and duration in seconds. It waits for
`/tmp/spacedock-replay-go`, moves without clicking, then removes the device.
The starting position is calibrated for the Cosmo test display; check the native
pointer bounds reported by `--benchmark-pointer` before comparing results on
another display. Configure acceleration only on the virtual test device.
An optional fifth argument sets the initial horizontal offset from the left
edge. On the Cosmo, `125 1 650 26 200` sweeps beyond both dock ends; the recorder
also reports intervals between consecutive inside-hover frames to separate
active hover timing from deliberate idle time outside the dock.

To see why individual frames are slow, set `SPACEDOCK_BENCHMARK_TRACE` to a
file path when running either benchmark. At the end of the recording, the dock
writes one line per event: a type letter, nanoseconds since recording started,
and a value. The letters are `Q` frame request, `M` mouse move, `A` after
animations (value bit 0: pointer inside, bit 1: pointer moved), `S`/`s` sync
start and end, `W` swap, `k`/`K` input-region update start and end, and `T` a
4 ms GUI-thread heartbeat; gaps in `T` show when the GUI thread is busy.

To profile QML, set `SPACEDOCK_QML_PROFILE` to a port, then attach with
`qmlprofiler --interactive --attach 127.0.0.1 --port <port>`. The dock starts
normally and does not wait for the profiler. In the profiler, `r` toggles
recording, `f <file>` writes the trace, and `q` quits.

### Hover animation

![Hover animation](assets/videos/hovering.gif)

### Add and remove shortcuts

![Adding and removing shortcuts](assets/videos/addandremove.gif)

### Rearrange launchers

![Rearranging launchers](assets/videos/moving.gif)

### AppImage handling

![AppImage handling](assets/videos/appimage.gif)

## Licensing & Notices

SpaceDock's source code and documentation are available under the [MIT License](LICENSE). You may use, modify, and distribute them, including commercially, while retaining the copyright and license notice. Assets are listed separately in [assets/MANIFEST.csv](assets/MANIFEST.csv); their distribution terms have not yet been finalized. Third-party dependencies have their own licenses.

Firefox and the Firefox logo are trademarks of the Mozilla Foundation in the U.S. and other countries. Other application names and logos belong to their respective owners. SpaceDock is an independent project and is not affiliated with or endorsed by those owners.
