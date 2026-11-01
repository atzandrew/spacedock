#!/usr/bin/env python3
"""Align clips by trigger frame and render a private side-by-side or 50% overlay."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('dock', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--reference-trigger', type=int, required=True)
    parser.add_argument('--dock-trigger', type=int, required=True)
    parser.add_argument('--fps', type=int, required=True, help='Common output rate; e.g. 120. Inputs retain their own timestamps.')
    parser.add_argument('--size', default='960:540', help='Common WIDTH:HEIGHT after scaling and padding')
    parser.add_argument('--mode', choices=['side-by-side', 'overlay'], default='side-by-side')
    args = parser.parse_args()
    if args.fps <= 0 or min(args.reference_trigger, args.dock_trigger) < 0:
        parser.error('FPS must be positive and trigger frames nonnegative')
    if not all(p.is_file() for p in (args.reference, args.dock)):
        parser.error('Both input clips must exist')
    if args.output.resolve() in (args.reference.resolve(), args.dock.resolve()):
        parser.error('Output must differ from input clips')
    import re
    if not re.fullmatch(r'[1-9][0-9]*:[1-9][0-9]*', args.size):
        parser.error('Size must be WIDTH:HEIGHT')
    width, height = args.size.split(':')
    chains = []
    for i, frame in enumerate((args.reference_trigger, args.dock_trigger)):
        chains.append(f'[{i}:v]trim=start_frame={frame},setpts=PTS-STARTPTS,fps={args.fps},'
                      f'scale={width}:{height}:force_original_aspect_ratio=decrease,'
                      f'pad={width}:{height}:(ow-iw)/2:(oh-ih)/2,setsar=1[v{i}]')
    combine = 'hstack=inputs=2:shortest=1' if args.mode == 'side-by-side' else 'blend=all_expr=0.5*A+0.5*B:shortest=1'
    chains.append(f'[v0][v1]{combine}[out]')
    subprocess.run(['ffmpeg', '-nostdin', '-n', '-i', str(args.reference), '-i', str(args.dock),
                    '-filter_complex', ';'.join(chains), '-map', '[out]', '-an',
                    '-c:v', 'ffv1', '-pix_fmt', 'yuv444p', str(args.output)], check=True)


if __name__ == '__main__':
    main()
