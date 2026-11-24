#!/usr/bin/env python3
"""Explain slow frames in a SpaceDock hover trace.

The trace comes from SPACEDOCK_BENCHMARK_TRACE with --benchmark-pointer or
--benchmark-hover (tools/profile-hover.py records one). Each line is
"TYPE NANOSECONDS VALUE":

  M  native mouse move reached the window (GUI thread)
  Q  Qt delivered UpdateRequest: the GUI thread starts a frame
  A  animations advanced; value bit 0 = pointer inside, bit 1 = pointer moved
  S  scene graph sync begins (GUI thread blocked)
  s  scene graph sync ends
  R  render thread starts drawing; r  drawing submitted
  W  Qt finished swapping the frame
  T  4 ms GUI heartbeat timer; a gap means the GUI thread was busy or starved
  k  input region requested; K  input region actually changed

Each swap is traced back through the sync that produced it to the animation
step and frame start before that: with Qt's threaded renderer the next frame
is often synced right after a swap, and the render thread then waits inside
the swap. Swap times are Qt's view, not compositor presentation. On Wayland,
Qt waits for the compositor's frame callback both before starting a frame
("wait") and inside the swap ("swap").
"""
import argparse
import bisect
import statistics
import sys
from collections import Counter
from pathlib import Path

PHASES = ("wait", "animate", "polish", "sync", "render", "swap")
CAUSES = {
    "gui-busy": "GUI thread busy between frames (timers, Qt.callLater, model or input-region work)",
    "input-late": "no pointer events arrived in time (compositor or input delivery)",
    "frame-pacing": "GUI idle and input present, frame not started (compositor frame callback / vsync)",
    "animate": "frame work: animations, FrameAnimation handlers and the JavaScript they run",
    "polish": "polish and item layout, or waiting for the render thread to accept the frame",
    "sync": "scene graph sync with the GUI blocked (node updates, texture uploads)",
    "render": "render thread drawing the scene graph",
    "swap": "render thread blocked in swap: compositor frame callback or GPU",
}


def load(path):
    marks = []
    for line in Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            marks.append((parts[0], int(parts[1]) / 1e6, float(parts[2])))
    marks.sort(key=lambda mark: mark[1])
    return marks


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))] if ordered else 0.0


def frames(marks):
    """One record per swap interval (previous W -> this W), traced backwards."""
    times = {kind: [time for k, time, _ in marks if k == kind] for kind in "QASsRrW"}
    values = {time: value for kind, time, value in marks if kind == "A"}

    def last(kind, before, after=float("-inf")):
        series = times[kind]
        index = bisect.bisect_right(series, before) - 1
        return series[index] if index >= 0 and series[index] >= after else None

    result = []
    swaps = times["W"]
    position = 0
    for previous, end in zip(swaps, swaps[1:]):
        record = {"start": previous, "end": end}
        sync_end = last("s", end)
        sync_begin = last("S", sync_end) if sync_end is not None else None
        animated = last("A", sync_begin) if sync_begin is not None else None
        started = last("Q", animated) if animated is not None else None
        render_begin = last("R", end, sync_end) if sync_end is not None else None
        render_end = last("r", end, render_begin) if render_begin is not None else None
        for key, value in (("Q", started), ("A", animated), ("S", sync_begin), ("s", sync_end),
                           ("R", render_begin), ("r", render_end)):
            if value is not None:
                record[key] = value
        if animated is not None:
            record["inside"] = bool(int(values[animated]) & 1)
            record["moved"] = bool(int(values[animated]) & 2)
        while position < len(marks) and marks[position][1] <= previous:
            position += 1
        events = []
        index = position
        while index < len(marks) and marks[index][1] <= end:
            if marks[index][0] != "W":
                events.append(marks[index])
            index += 1
        record["events"] = events
        result.append(record)
    return result


def phase_times(frame):
    start, end = frame["start"], frame["end"]
    q = frame.get("Q", start)
    a = frame.get("A", q)
    s_begin = frame.get("S", a)
    s_end = frame.get("s", s_begin)
    r_begin = frame.get("R", s_end)
    r_end = frame.get("r", r_begin)
    return {
        "wait": max(0.0, q - start),
        "animate": max(0.0, a - q),
        "polish": max(0.0, s_begin - a),
        "sync": max(0.0, s_end - s_begin),
        "render": max(0.0, r_end - s_end),
        "swap": max(0.0, end - r_end),
    }


def heartbeat_gap(heartbeats, start, end):
    """Longest GUI heartbeat gap overlapping [start, end]."""
    longest = 0.0
    for first, second in zip(heartbeats, heartbeats[1:]):
        if second < start or first > end:
            continue
        longest = max(longest, min(second, end) - max(first, start))
    return longest


def classify(frame, phases, heartbeats, period):
    dominant = max(PHASES, key=lambda name: phases[name])
    if dominant != "wait":
        return dominant
    wait_end = max(frame["start"], frame.get("Q", frame["end"]))
    if heartbeat_gap(heartbeats, frame["start"], wait_end) > period:
        return "gui-busy"
    moves = [time for kind, time, _ in frame["events"] if kind == "M" and time <= wait_end]
    if not moves or moves[0] - frame["start"] > period:
        return "input-late"
    return "frame-pacing"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace")
    parser.add_argument("--refresh-hz", type=float, default=60.0)
    parser.add_argument("--slow-factor", type=float, default=1.5,
                        help="an interval is slow above this many refresh periods (default 1.5)")
    parser.add_argument("--timelines", type=int, default=8, help="slow frames to print in detail")
    parser.add_argument("--all-frames", action="store_true",
                        help="include frames with the pointer outside or still")
    args = parser.parse_args()

    marks = load(args.trace)
    if not marks:
        sys.exit(f"{args.trace}: no trace marks")
    period = 1000.0 / args.refresh_hz
    threshold = period * args.slow_factor
    heartbeats = [time for kind, time, _ in marks if kind == "T"]
    all_frames = frames(marks)
    # Hover smoothness: consecutive frames with the pointer inside, this one moving.
    selected = []
    for index, frame in enumerate(all_frames):
        if args.all_frames or (frame.get("inside") and frame.get("moved")
                               and index > 0 and all_frames[index - 1].get("inside")):
            selected.append(frame)
    if not selected:
        sys.exit("no hover frames found; was the pointer over the dock?")

    intervals = [frame["end"] - frame["start"] for frame in selected]
    span = (marks[-1][1] - marks[0][1]) / 1000
    print(f"trace {Path(args.trace).name}: {span:.1f}s, {len(all_frames)} swaps, "
          f"{len(selected)} hover frames, refresh {args.refresh_hz:g} Hz (slow > {threshold:.1f} ms)")
    slow = [frame for frame in selected if frame["end"] - frame["start"] > threshold]
    missed = sum(max(0, round((frame["end"] - frame["start"]) / period) - 1) for frame in slow)
    print(f"hover interval ms: median {statistics.median(intervals):.2f}  p95 {percentile(intervals, .95):.2f}  "
          f"p99 {percentile(intervals, .99):.2f}  max {max(intervals):.2f}")
    print(f"slow frames: {len(slow)} of {len(selected)} ({100 * len(slow) / len(selected):.1f}%), "
          f"about {missed} refreshes missed")

    print("\nphase ms over hover frames      median     p95     max")
    all_phases = [phase_times(frame) for frame in selected]
    for name in PHASES:
        values = [phases[name] for phases in all_phases]
        print(f"  {name:<28}{statistics.median(values):8.2f}{percentile(values, .95):8.2f}{max(values):8.2f}")

    gaps = [second - first for first, second in zip(heartbeats, heartbeats[1:])]
    if gaps:
        print(f"\nGUI heartbeat: {len(heartbeats) / max(span, 1e-9):.0f}/s (ideal 250), "
              f"gaps > {period:.0f} ms: {sum(gap > period for gap in gaps)}, longest {max(gaps):.1f} ms")
    regions = sum(1 for kind, _, _ in marks if kind == "k")
    changes = sum(1 for kind, _, _ in marks if kind == "K")
    if regions:
        print(f"input region: {regions} requests, {changes} changed the mask")

    if slow:
        causes = Counter(classify(frame, phase_times(frame), heartbeats, period) for frame in slow)
        print("\nslow frames by cause:")
        for cause, count in causes.most_common():
            print(f"  {count:5d}  {cause:<13} {CAUSES[cause]}")

        print(f"\nslowest {min(args.timelines, len(slow))} frames (ms after previous swap):")
        for frame in sorted(slow, key=lambda f: f["start"] - f["end"])[:args.timelines]:
            phases = phase_times(frame)
            cause = classify(frame, phases, heartbeats, period)
            moves = sum(1 for kind, _, _ in frame["events"] if kind == "M")
            gap = heartbeat_gap(heartbeats, frame["start"], frame["end"])
            summary = " ".join(f"{name} {phases[name]:.1f}" for name in PHASES)
            print(f"  at {(frame['start'] - marks[0][1]) / 1000:6.2f}s  {frame['end'] - frame['start']:5.1f} ms  "
                  f"[{cause}]  {summary}  moves {moves}  heartbeat gap {gap:.1f}")
            events = [f"{kind}{time - frame['start']:.1f}" for kind, time, _ in frame["events"]
                      if kind not in "T"]
            print("      " + " ".join(events[:40]) + (" ..." if len(events) > 40 else ""))


if __name__ == "__main__":
    main()
