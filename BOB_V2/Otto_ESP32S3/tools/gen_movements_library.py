#!/usr/bin/env python3
"""Generate movements_library.h for the BOB ESP32-S3 firmware.

All moves are authored as unit-vector offsets in [-1,1] per joint, then scaled
by an amplitude and rebased around the given --neutral idle pose.  Every move
starts and ends at the neutral pose so they are safe to play in any order.

Usage (from repo root):
    python3 BOB_V2/Otto_ESP32S3/tools/gen_movements_library.py \
        --neutral 54 71 71 86 \
        --amplitude 28 \
        --output BOB_V2/Otto_ESP32S3/movements_library.h
"""
import argparse
import textwrap
from pathlib import Path

# Joint order: LH, LF, RH, RF
JOINTS = 4


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def rebase(offsets, neutral, amplitude, lo=0, hi=180):
    """Convert unit offsets → absolute angles clamped to [lo,hi]."""
    return [round(clamp(neutral[i] + offsets[i] * amplitude, lo, hi)) for i in range(JOINTS)]


def make_frames(neutral, poses, amplitude, move_ms, hold_ms,
                first_move_ms=None, last_move_ms=None):
    """Build the frame list: home → pose1 → pose2 … → home.
    
    Each element is (a0,a1,a2,a3, moveMs, holdMs).
    """
    frames = []
    # First frame: settle at home
    frames.append((*neutral, first_move_ms or move_ms, 63))
    for i, offs in enumerate(poses):
        ang = rebase(offs, neutral, amplitude)
        ms  = move_ms
        hms = hold_ms
        frames.append((*ang, ms, hms))
    # Return home
    frames.append((*neutral, last_move_ms or move_ms, 90))
    return frames


def define_moves(N, A):
    """All 33 moves.  N=neutral list, A=amplitude.
    
    Returns list of (name, expression, frames_list).
    Expressions: 0=happy,1=sad,2=angry,3=curious,4=sleepy,5=love,6=normal,-1=none
    """
    H = 120   # normal moveMs
    Q = 200   # slower moveMs for big swings
    K = 300   # walk/gait step ms

    # Gait amplitude is fixed relative to 90° centre; we translate it to be
    # relative to the neutral pose by computing gait offsets as (absolute - 90)
    # then adding to neutral.  This preserves gait geometry.
    def gait(absolute):
        """Convert a gait frame that was designed for neutral=90 to this neutral."""
        return [clamp(N[i] + (absolute[i] - 90), 0, 180) for i in range(JOINTS)]

    moves = []

    # ── Small gentle idle fidgets ──────────────────────────────────────────────

    # 0 Breathe
    moves.append(("Breathe", 6, make_frames(N, [
        [.3, .3, .3, .3],
        [0,  0,  0,  0],
    ], A, 120, 36, first_move_ms=120, last_move_ms=120)))

    # 1 Little nod
    moves.append(("Little nod", 0, make_frames(N, [
        [.6, 0, .6, 0],
        [-.4, 0, -.4, 0],
    ], A, 120, 36, last_move_ms=120)))

    # 2 Look around
    moves.append(("Look around", 3, make_frames(N, [
        [.5,  .3,  .5, -.3],
        [-.5, -.3, -.5, .3],
        [0, 0, 0, 0],
    ], A, 144, 36, first_move_ms=120, last_move_ms=120)))

    # 3 Sleepy rock
    moves.append(("Sleepy rock", 4, make_frames(N, [
        [.4, .6, .4, -.6],
        [-.4, -.6, -.4, .6],
    ], A, 176, 36, first_move_ms=120, last_move_ms=120)))

    # 4 Curious
    moves.append(("Curious", 3, make_frames(N, [
        [.2, -.3, -.1, .3],
        [1,  .6, -1, -.6],
        [-.6, .3, .9, -.3],
        [0, 0, 0, 0],
    ], A, 120, 61, first_move_ms=120, last_move_ms=120)))

    # 5 Curious lean
    moves.append(("Curious lean", 3, make_frames(N, [
        [.7, .8, .7, -.8],
        [0, 0, 0, 0],
    ], A, 120, 36, first_move_ms=120, last_move_ms=120)))

    # 6 Peek
    moves.append(("Peek", 3, make_frames(N, [
        [.7, -.3, -.7, .3],
    ], A, 120, 58, first_move_ms=120, last_move_ms=120)))

    # 7 Sneaky tiptoe
    moves.append(("Sneaky tiptoe", 3, make_frames(N, [
        [.5,  .3,  -.2, -.3],
        [-.2, -.3,  .5,  .3],
        [.5,  .3,  -.2, -.3],
        [-.2, -.3,  .5,  .3],
    ], A, 120, 22, first_move_ms=120, last_move_ms=120)))

    # ── Big expressive moves ───────────────────────────────────────────────────

    # 8 Happy wiggle
    moves.append(("Happy wiggle", 0, make_frames(N, [
        [1,  .6, -1, -.6],
        [-1, -.6, 1,  .6],
        [1,  .6, -1, -.6],
    ], A, 144, 36, first_move_ms=120, last_move_ms=144)))

    # 9 Wiggle
    moves.append(("Wiggle", 0, make_frames(N, [
        [.7,  .5, -.7, -.5],
        [-.7, -.5, .7,  .5],
        [.7,  .5, -.7, -.5],
        [-.7, -.5, .7,  .5],
    ], A, 168, 11, first_move_ms=120, last_move_ms=120)))

    # 10 Excited hop
    moves.append(("Excited hop", 0, make_frames(N, [
        [-.5, -.5, -.5, -.5],
        [ 1,   1,   1,   1],
        [.15, .15, .15, .15],
        [ .7,  .7,  .7,  .7],
    ], A, 120, 9, first_move_ms=120, last_move_ms=120)))

    # 11 Celebrate
    moves.append(("Celebrate", 5, make_frames(N, [
        [ .6,  .6,  .6,  .6],
        [-.5, -.5, -.5, -.5],
        [ 1,  0, -1,  0],
        [-1,  0,  1,  0],
        [ 1,  0, -1,  0],
        [-1,  0,  1,  0],
        [ .8,  .8,  .8,  .8],
    ], A, 120, 9, first_move_ms=120, last_move_ms=120)))

    # 12 Sad droop
    moves.append(("Sad droop", 1, make_frames(N, [
        [-.5, -.5, -.5, -.5],
        [-.7, -.7, -.7, -.7],
    ], A, 120, 90, first_move_ms=120, last_move_ms=120)))

    # 13 Shiver
    moves.append(("Shiver", 1, make_frames(N, [
        [-.3,  .3, -.3,  .3],
        [ .3, -.3,  .3, -.3],
        [-.3,  .3, -.3,  .3],
        [ .3, -.3,  .3, -.3],
        [-.3,  .3, -.3,  .3],
        [ .3, -.3,  .3, -.3],
    ], A, 120, 7, first_move_ms=120, last_move_ms=120)))

    # 14 Hiccup
    moves.append(("Hiccup", 3, make_frames(N, [
        [ .6,  .8,  .6,  .8],
        [0, 0, 0, 0],
        [ .7,  .9,  .7,  .9],
    ], A, 120, 11, first_move_ms=120, last_move_ms=124)))

    # 15 Double take
    moves.append(("Double take", 3, make_frames(N, [
        [-.5,  .3,  .7, -.3],
        [0, 0, 0, 0],
        [-.5,  .3,  .7, -.3],
    ], A, 120, 22, first_move_ms=120, last_move_ms=120)))

    # ── Coordinated / big body moves ──────────────────────────────────────────

    # 16 Bounce (sync)
    moves.append(("Bounce (sync)", 0, make_frames(N, [
        [1,  1,  1,  1],
        [-1, -1, -1, -1],
        [1,  1,  1,  1],
        [-1, -1, -1, -1],
    ], A, 144, 36, first_move_ms=120, last_move_ms=144)))

    # 17 Foot flap
    moves.append(("Foot flap", 0, make_frames(N, [
        [ .4,  1,  .4,  1],
        [-.4, -1, -.4, -1],
        [ .4,  1,  .4,  1],
        [-.4, -1, -.4, -1],
    ], A, 144, 36, first_move_ms=120, last_move_ms=144)))

    # 18 Foot dance  (complex 14-frame sequence)
    moves.append(("Foot dance", 0, make_frames(N, [
        [ .2,  .8,  .2,  .8],
        [0,  1.7, 0, -1.7],  # will be clamped
        [0, -1.7, 0,  1.7],
        [0,  1.7, 0, -1.7],
        [0, -1.7, 0,  1.7],
        [ .15,  1.7, .15, 1.7],
        [-.15, -1.7, -.15, -1.7],
        [ .15,  1.7, .15, 1.7],
        [-.15, -1.7, -.15, -1.7],
        [ 1,  1.5, -1, -1.5],
        [-1, -1.5,  1,  1.5],
        [ .3,  1.8,  .3, 1.8],
    ], A, 120, 11, first_move_ms=120, last_move_ms=208)))

    # 19 Waddle
    moves.append(("Waddle", 0, make_frames(N, [
        [1,  .5, -1, -.5],
        [-1, -.5, 1,  .5],
        [1,  .5, -1, -.5],
        [-1, -.5, 1,  .5],
    ], A, 144, 36, first_move_ms=120, last_move_ms=144)))

    # 20 Cha-cha
    moves.append(("Cha-cha", 0, make_frames(N, [
        [ .6,  .5, -.5, -.5],
        [0, 0, 0, 0],
        [-.5, -.5, .6,  .5],
        [0, 0, 0, 0],
        [ .7,  .8,  .7,  .8],
        [-.6, -.7, -.6, -.7],
    ], A, 120, 13, first_move_ms=120, last_move_ms=120)))

    # 21 Moonwalk slide
    moves.append(("Moonwalk slide", 5, make_frames(N, [
        [ .7,  1,   .6, -.8],
        [ .6, -.8,  .7,  1],
        [ .7,  1,   .6, -.8],
        [ .6, -.8,  .7,  1],
    ], A, 208, 11, first_move_ms=120, last_move_ms=120)))

    # 22 Leg wave
    moves.append(("Leg wave", 5, make_frames(N, [
        [ 1, 0, 0, 0],
        [0,  1, 0, 0],
        [0, 0,  1, 0],
        [0, 0, 0,  1],
        [0, 0, 0, 0],
    ], A, 144, 36, first_move_ms=120, last_move_ms=144)))

    # 23 Robot
    moves.append(("Robot", 6, make_frames(N, [
        [1.5,  -.8, -.6,  0],
        [-.6,  1.5,  1.5, -.8],
        [1.5,  1.5, -.6, -.6],
        [-.6, -.6,  1.5,  1.5],
        [1.5,  0,  -.6,  0],
        [0,  1.5,  0, -.6],
        [ .8, -.4,  .8, -.4],
    ], A, 168, 13, first_move_ms=120, last_move_ms=120)))

    # 24 Dizzy
    moves.append(("Dizzy", 4, make_frames(N, [
        [1.5,  .6, 0, 0],
        [ .6, 1.5, 1.5,  .6],
        [0, 0,  .6, 1.5],
        [-.7, -.4, 1.5, 1.5],
        [-.8, -.7, 0, 0],
        [-.4, -.4, -.4, -.4],
    ], A, 176, 13, first_move_ms=120, last_move_ms=124)))

    # 25 Sneeze
    moves.append(("Sneeze", 3, make_frames(N, [
        [-.4,  .5, -.4,  .5],
        [-.6,  .8, -.6,  .8],
        [-.8,  .9, -.8,  .9],
        [1.7, -.5, 1.7, -.5],
        [ .3,  .1,  .3,  .1],
    ], A, 120, 13, first_move_ms=120, last_move_ms=120)))

    # 26 Kung fu
    moves.append(("Kung fu", 2, make_frames(N, [
        [1.7, -.4, 0,  1.1],
        [0,    0,  0,   0],
        [0,  1.1, 1.7, -.4],
        [0,    0,  0,   0],
        [1.5,  1.5, -.6, -.6],
        [-.6, -.6, 1.5,  1.5],
    ], A, 196, 13, first_move_ms=120, last_move_ms=176)))

    # 27 Tantrum
    moves.append(("Tantrum", 2, make_frames(N, [
        [0,  1.7, 0, -.8],
        [0, -.8,  0,  1.7],
        [0,  1.7, 0, -.8],
        [0, -.8,  0,  1.7],
        [1.5,  1.5, 1.5, 1.5],
        [-.6, -.6, -.6, -.6],
        [1.5, -.6, -.6, 1.5],
    ], A, 196, 13, first_move_ms=120, last_move_ms=176)))

    # 28 Kick
    moves.append(("Kick", 2, make_frames(N, [
        [ .4,  .4, -.5, -.4],
        [1.5,  1.7, -.5, -.4],
        [0, 0, 0, 0],
        [-.5, -.4,  .4,  .4],
        [-.5, -.4, 1.5,  1.7],
    ], A, 120, 16, first_move_ms=120, last_move_ms=208)))

    # 29 One leg stand
    moves.append(("One leg stand", 3, make_frames(N, [
        [ .7,  .7, -.4, -.4],
        [1.5,  1.7, -.4, -.4],
        [ .7,  .7, -.4, -.4],
        [0, 0, 0, 0],
        [-.4, -.4,  .7,  .7],
        [-.4, -.4, 1.5, 1.7],
        [-.4, -.4,  .7,  .7],
    ], A, 128, 25, first_move_ms=120, last_move_ms=128)))

    # ── Gait moves — designed around 90° centre, translated to this neutral ───
    # Walk forward: hip swing + foot roll, home-relative
    def w(lh, lf, rh, rf, ms=K, hms=20):
        return (clamp(N[0]+(lh-90), 0, 180),
                clamp(N[1]+(lf-90), 0, 180),
                clamp(N[2]+(rh-90), 0, 180),
                clamp(N[3]+(rf-90), 0, 180),
                ms, hms)

    # 30 Walk forward
    bm30 = [
        (*N, 300, 60),
        w(68, 116, 120, 116), w(68,  64, 120,  64),
        w(120,  64,  68,  64), w(120, 116,  68, 116),
        w( 68, 116, 120, 116), w( 68,  64, 120,  64),
        w(120,  64,  68,  64), w(120, 116,  68, 116),
        w( 68, 116, 120, 116), w( 68,  64, 120,  64),
        w(120,  64,  68,  64), w(120, 116,  68, 116),
        (*N, 360, 180),
    ]
    moves.append(("Walk forward", 6, bm30))

    # 31 Turn left
    bm31 = [
        (*N, 300, 60),
        w(87, 116, 120, 116), w(87,  64, 120,  64),
        w(101,  64,  68,  64), w(101, 116,  68, 116),
        w( 87, 116, 120, 116), w( 87,  64, 120,  64),
        w(101,  64,  68,  64), w(101, 116,  68, 116),
        w( 87, 116, 120, 116), w( 87,  64, 120,  64),
        w(101,  64,  68,  64), w(101, 116,  68, 116),
        (*N, 360, 180),
    ]
    moves.append(("Turn left", 6, bm31))

    # 32 Turn right
    bm32 = [
        (*N, 300, 60),
        w( 68, 116, 101, 116), w( 68,  64, 101,  64),
        w(120,  64,  87,  64), w(120, 116,  87, 116),
        w( 68, 116, 101, 116), w( 68,  64, 101,  64),
        w(120,  64,  87,  64), w(120, 116,  87, 116),
        w( 68, 116, 101, 116), w( 68,  64, 101,  64),
        w(120,  64,  87,  64), w(120, 116,  87, 116),
        (*N, 360, 180),
    ]
    moves.append(("Turn right", 6, bm32))

    return moves


def frames_to_c(frames):
    parts = []
    for f in frames:
        a0, a1, a2, a3, ms, hms = f
        parts.append("{%d,%d,%d,%d,%d,%d}" % (
            clamp(int(round(a0)), 0, 255),
            clamp(int(round(a1)), 0, 255),
            clamp(int(round(a2)), 0, 255),
            clamp(int(round(a3)), 0, 255),
            int(ms), int(hms),
        ))
    return ",".join(parts)


def generate(neutral, amplitude, out_path):
    N = neutral
    moves = define_moves(N, amplitude)

    lines = [
        "#pragma once",
        f"// Auto-generated by gen_movements_library.py — neutral {N}, amplitude {amplitude}",
        f"// All moves start and end at neutral {N}. Regenerate when idle pose changes.",
        "#include <pgmspace.h>",
        "struct BuiltinFrame { uint8_t a0,a1,a2,a3; uint16_t moveMs, holdMs; };",
        "struct BuiltinMove { const char* name; int8_t expression; uint8_t nframes; const BuiltinFrame* frames; };",
    ]

    for i, (name, expr, frames) in enumerate(moves):
        lines.append(f"static const BuiltinFrame bm_{i}[] PROGMEM = {{{frames_to_c(frames)}}};")

    lines.append("static const BuiltinMove BUILTIN_MOVES[] PROGMEM = {")
    for i, (name, expr, frames) in enumerate(moves):
        esc = name.replace('"', '\\"')
        nf  = len(frames)
        lines.append(f'  {{"{esc}",{expr},{nf},bm_{i}}},')
    lines.append("};")
    lines.append(f"static const unsigned BUILTIN_MOVE_COUNT = {len(moves)};")

    content = "\n".join(lines) + "\n"
    Path(out_path).write_text(content)
    print(f"Wrote {len(moves)} moves to {out_path}  (neutral={N}, amplitude={amplitude})")
    return moves


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--neutral", nargs=4, type=int, required=True,
                    metavar=("LH", "LF", "RH", "RF"))
    ap.add_argument("--amplitude", type=int, default=28,
                    help="Move size in degrees (default 28)")
    ap.add_argument("--output", type=Path,
                    default=Path("BOB_V2/Otto_ESP32S3/movements_library.h"))
    args = ap.parse_args()
    generate(args.neutral, args.amplitude, args.output)


if __name__ == "__main__":
    main()
