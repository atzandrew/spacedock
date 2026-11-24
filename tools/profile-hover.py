#!/usr/bin/env python3
"""Replay a real mouse sweep over the dock and explain dropped frames.

Runs one build with --benchmark-pointer and a frame trace, moves a temporary
virtual mouse (tools/replay-pointer.cpp, through /dev/uinput) back and forth
across the icon row, then summarizes Qt's frame timing and the cause of each
slow frame (tools/analyze-hover-trace.py). Run it from a terminal inside the
Plasma Wayland session; it needs write access to /dev/uinput.

Only one dock can run at a time, so a running SpaceDock is stopped first and
started again afterwards. The virtual mouse only moves: it never clicks.
Keep your own mouse still while the sweep runs.

Examples:
  tools/profile-hover.py                          # build/spacedock, idle machine
  tools/profile-hover.py --cpu-load 8 --label busy
  tools/profile-hover.py --binary ~/.local/bin/spacedock --label installed
  tools/profile-hover.py --qml-profile            # adds a QML hot-spot list
"""
import argparse
import collections
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEVICE_NAME = "SpaceDock Repeatable Mouse"
GEOMETRY = re.compile(r"hover-benchmark: geometry surface ([\d.]+)x([\d.]+) row-left (-?[\d.]+) "
                      r"row-width ([\d.]+) icon-bottom ([\d.]+) icon-size ([\d.]+) refresh-hz ([\d.]+)")


def fail(message):
    sys.exit(f"profile-hover: {message}")


def wait_for(predicate, timeout, interval=0.05):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    return None


def replay_helper(build):
    source = ROOT / "tools/replay-pointer.cpp"
    helper = build / "replay-pointer"
    if not helper.exists() or helper.stat().st_mtime < source.stat().st_mtime:
        compiler = shutil.which("c++") or fail("no C++ compiler to build tools/replay-pointer.cpp")
        subprocess.run([compiler, "-O2", "-std=c++20", str(source), "-o", str(helper)], check=True)
    return helper


def running_docks():
    docks = []
    for pid in subprocess.run(["pgrep", "-x", "spacedock"], capture_output=True, text=True).stdout.split():
        try:
            exe = os.readlink(f"/proc/{pid}/exe").removesuffix(" (deleted)")
            args = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")[1:-1]
            docks.append((int(pid), exe, [arg.decode() for arg in args]))
        except OSError:
            pass
    return docks


def stop_process(pid, timeout=8):
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    if not wait_for(lambda: not Path(f"/proc/{pid}").exists(), timeout):
        os.kill(pid, signal.SIGKILL)


def virtual_device():
    for device in Path("/sys/class/input").glob("event*"):
        try:
            if (device / "device/name").read_text().strip() == DEVICE_NAME:
                return device.name
        except OSError:
            pass
    return None


def flatten_acceleration(event):
    """Make one count move one logical pixel, so the sweep lands on the icon row."""
    path = f"/org/kde/KWin/InputDevice/{event}"
    for prop, kind, value in (("pointerAccelerationProfileFlat", "b", "true"), ("pointerAcceleration", "d", "0")):
        subprocess.run(["busctl", "--user", "set-property", "org.kde.KWin", path,
                        "org.kde.KWin.InputDevice", prop, kind, value], capture_output=True)


def thread_times(pid):
    """CPU clock ticks per thread name for one process."""
    totals = collections.Counter()
    for task in Path(f"/proc/{pid}/task").glob("*"):
        try:
            name = (task / "comm").read_text().strip()
            fields = (task / "stat").read_text().rsplit(")", 1)[1].split()
            totals[name] += int(fields[11]) + int(fields[12])
        except (OSError, IndexError, ValueError):
            pass
    return totals


def qml_hot_spots(path, limit=25):
    """Self time per QML location from a qmlprofiler .qtd file."""
    keep = {"Binding", "Javascript", "HandlingSignal", "Creating"}
    names, ranges = {}, []
    for _, element in ElementTree.iterparse(path):
        if element.tag == "event":
            if element.findtext("type") in keep:
                location = element.findtext("filename", "").replace("qrc:/qml/", "")
                details = (element.findtext("details") or "")[:60]
                names[int(element.get("index"))] = (f"{element.findtext('type')} {location}:"
                                                     f"{element.findtext('line')} {details}")
            element.clear()
        elif element.tag == "range":
            index = int(element.get("eventIndex"))
            if index in names and element.get("duration"):
                start = int(element.get("startTime"))
                ranges.append((start, start + int(element.get("duration")), index))
            element.clear()
    if not ranges:
        return "no QML ranges recorded"
    ranges.sort(key=lambda r: (r[0], -r[1]))
    own, total, calls, stack = collections.Counter(), collections.Counter(), collections.Counter(), []
    for start, end, index in ranges:
        while stack and stack[-1][1] <= start:
            stack.pop()
        total[index] += end - start
        own[index] += end - start
        calls[index] += 1
        if stack:
            own[stack[-1][2]] -= end - start
        stack.append((start, end, index))
    seconds = (ranges[-1][1] - ranges[0][0]) / 1e9
    lines = [f"QML hot spots over {seconds:.1f}s (profiler overhead inflates all numbers)",
             f"{'self ms':>9} {'incl ms':>9} {'calls':>8}  location"]
    for index, value in own.most_common(limit):
        lines.append(f"{value / 1e6:9.1f} {total[index] / 1e6:9.1f} {calls[index]:8d}  {names[index]}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=str(ROOT / "build/spacedock"), help="dock build to measure")
    parser.add_argument("--label", default=time.strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--out", help="output directory (default build/hover-profiles/LABEL)")
    parser.add_argument("--period", type=float, default=1.0, help="seconds per left-right-left sweep")
    parser.add_argument("--rate", type=int, default=125, help="virtual mouse reports per second")
    parser.add_argument("--overshoot", type=float, default=40,
                        help="pixels to travel past each end of the row (enter/exit magnification)")
    parser.add_argument("--cpu-load", type=int, default=0, metavar="N",
                        help="keep N busy processes running, like a heavy browser")
    parser.add_argument("--qml-profile", action="store_true",
                        help="also record a QML profile (slows the dock; frame numbers are then not comparable)")
    parser.add_argument("--aim", nargs=3, type=float, metavar=("START_X", "SPAN", "UP"),
                        help="sweep these pixels instead of the row the dock reports; use the go file of a "
                             "previous run to measure builds that predate the geometry report")
    parser.add_argument("--refresh-hz", type=float, help="display refresh rate if the dock does not report it")
    parser.add_argument("--no-restart", action="store_true", help="leave the previous dock stopped")
    parser.add_argument("dock_args", nargs="*", metavar="-- DOCK_ARG",
                        help="extra dock arguments after --, such as --tuning PATH")
    args = parser.parse_args()

    binary = Path(args.binary).expanduser().resolve()
    if not os.access(binary, os.X_OK):
        fail(f"{binary} is not executable; build it first")
    if os.environ.get("XDG_SESSION_TYPE") != "wayland" or not os.environ.get("WAYLAND_DISPLAY"):
        fail("run this from a terminal in the Plasma Wayland session")
    if not os.access("/dev/uinput", os.W_OK):
        fail("/dev/uinput is not writable. Grant access until the next reboot with:\n"
             f"  sudo setfacl -m u:{os.environ.get('USER', '$USER')}:rw /dev/uinput")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    helper = replay_helper(build)
    out = Path(args.out).expanduser() if args.out else build / "hover-profiles" / args.label
    out.mkdir(parents=True, exist_ok=True)
    trace, log, go = out / "trace.txt", out / "dock.log", out / "go"
    for stale in (trace, go):
        stale.unlink(missing_ok=True)

    previous = running_docks()
    children = []
    dock = None
    try:
        for pid, _, _ in previous:
            stop_process(pid)
        # Virtual mouse first, so KWin has registered it before the dock is ready.
        replay = subprocess.Popen([str(helper), str(args.rate), str(args.period), "100", "28"],
                                  env={**os.environ, "SPACEDOCK_REPLAY_GO": str(go)},
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        children.append(replay)
        event = wait_for(virtual_device, 5)
        if not event:
            fail("the virtual mouse did not appear")
        flatten_acceleration(event)

        env = {**os.environ, "SPACEDOCK_BENCHMARK_TRACE": str(trace), "QT_FORCE_STDERR_LOGGING": "1"}
        qml_port = 3768
        if args.qml_profile:
            env["SPACEDOCK_QML_PROFILE"] = str(qml_port)
        with open(log, "w") as output:
            dock = subprocess.Popen([str(binary), "--benchmark-pointer", *args.dock_args], env=env,
                                    stdout=output, stderr=subprocess.STDOUT)
        children.append(dock)
        match = wait_for(lambda: GEOMETRY.search(log.read_text()) or (args.aim and "hover-benchmark" in log.read_text()),
                         30, 0.02)
        if not match:
            fail(f"the dock did not report its geometry; see {log}, or pass --aim")
        refresh = args.refresh_hz or 60.0
        if args.aim:
            start, span, up = args.aim
        else:
            width, height, row_left, row_width, icon_bottom, icon_size, reported = map(float, match.groups())
            refresh = args.refresh_hz or reported
            start = max(0.0, row_left - args.overshoot)
            span = min(width - 1 - start, row_width + 2 * args.overshoot)
            # The mouse homes to the bottom-left pixel, then rises to the icon centers.
            up = max(0.0, height - 1 - (icon_bottom - icon_size / 2))
        go.write_text(f"{start:.0f} {span:.0f} {up:.0f}\n")
        print(f"sweeping x {start:.0f}..{start + span:.0f}, {up:.0f} px above the bottom edge, "
              f"{args.period:g}s per sweep at {args.rate} reports/s; recording 20s after a 2s warm-up")

        load = [subprocess.Popen([sys.executable, "-c", "while True: pass"]) for _ in range(args.cpu_load)]
        children.extend(load)
        profiler = None
        if args.qml_profile:
            profiler = subprocess.Popen([shutil.which("qmlprofiler") or "/usr/lib64/qt6/bin/qmlprofiler",
                                         "--interactive", "--attach", "127.0.0.1", "--port", str(qml_port),
                                         "--record", "off"], stdin=subprocess.PIPE,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True)
            children.append(profiler)
            time.sleep(3)
            profiler.stdin.write("r\n")
            profiler.stdin.flush()

        wait_for(lambda: "hover-benchmark: recording" in log.read_text(), 30, 0.05)
        threads_before, recording_started = thread_times(dock.pid), time.monotonic()
        finished = wait_for(lambda: "Qt swaps/sec" in log.read_text(), 40, 0.05)
        threads_after, recording_seconds = thread_times(dock.pid), time.monotonic() - recording_started
        if profiler:
            profiler.stdin.write(f"f {out / 'qml.qtd'}\n")
            profiler.stdin.flush()
            wait_for(lambda: (out / "qml.qtd").exists(), 30, 0.5)
            time.sleep(2)
            profiler.stdin.write("q\n")
            profiler.stdin.flush()
        if not finished:
            fail(f"the benchmark did not finish; see {log}")
        wait_for(lambda: "process CPU percent" in log.read_text(), 5, 0.05)
        time.sleep(0.5)
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
        for child in children:
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
        if not args.no_restart:
            for _, exe, dock_args in previous:
                subprocess.Popen(["setsid", "-f", exe, *dock_args], stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    report = [f"label {args.label}  binary {binary}",
              f"cpu load {args.cpu_load} processes  sweep period {args.period:g}s  rate {args.rate}/s"]
    replay_output = replay.stdout.read() if replay.stdout else ""
    report += [line for line in replay_output.splitlines() if line.startswith(("Sweep", "Replay"))]
    report += [line.split("hover-benchmark: ", 1)[1] for line in log.read_text().splitlines()
               if "hover-benchmark:" in line and "geometry" not in line]
    ticks = os.sysconf("SC_CLK_TCK") * max(recording_seconds, 1e-9) / 100
    usage = {name: (threads_after[name] - threads_before.get(name, 0)) / ticks for name in threads_after}
    busy = [f"{name} {percent:.1f}%" for name, percent in sorted(usage.items(), key=lambda item: -item[1])
            if percent >= 0.5]
    report.append("CPU per thread while recording (% of one core): " + (", ".join(busy) or "none"))
    analysis = subprocess.run([sys.executable, str(ROOT / "tools/analyze-hover-trace.py"), str(trace),
                               "--refresh-hz", f"{refresh:g}"], capture_output=True, text=True)
    report += ["", analysis.stdout.rstrip() or analysis.stderr.rstrip()]
    if args.qml_profile:
        qtd = out / "qml.qtd"
        report += ["", qml_hot_spots(qtd) if qtd.exists() else "QML profile was not written"]
    text = "\n".join(report) + "\n"
    (out / "summary.txt").write_text(text)
    print(text + f"\nsaved in {out}")


if __name__ == "__main__":
    main()
