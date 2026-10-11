#!/usr/bin/env python3
"""Generate a rich library of expressive BOB movements as importable JSON.

Never contacts or moves the robot. Produces two kinds of movement, both directly
importable in Motion Studio and saveable to an ESP32 slot:

  * joint movements  (mode "joints") — absolute joint-angle keyframes offset
    around the calibrated neutral pose, clamped to a +/-radius travel envelope.
  * posture movements (mode "tilt")  — target body orientation per frame
    (pitch/roll degrees); played with PID on, the four motors drive the IMU to
    each target. These are robust to servo calibration because they are authored
    in orientation space, not joint space.

Every movement starts and ends at rest (neutral pose / level orientation) so it
is safe to chain in an idle loop. The manifest tags each movement with a
category and whether it is idle-suitable, for later BOB_ROBO_CONVO idle use.

Usage:
  python3 prepare_expression_library.py --neutral 90 90 90 39 \
      --radius 30 --amplitude 18 --tilt-amplitude 6 \
      --output BOB_V2/movements/expression_library
"""
import argparse
import json
from pathlib import Path

JOINT_ORDER = ["LH", "LF", "RH", "RF"]


def joint_movements(neutral, radius, amplitude):
    """Offsets are unit vectors in [-1,1] per joint, scaled by `amplitude`."""
    a = amplitude
    # key, display name, expression face, idle-ok, category, [per-frame offsets]
    specs = [
        ("idle_hold",    "Idle hold",     6, True,  "idle",
            [[0, 0, 0, 0]]),
        ("breathe",      "Breathe",       6, True,  "idle",
            [[.3, .3, .3, .3], [0, 0, 0, 0]]),
        ("little_nod",   "Little nod",    0, True,  "gesture",
            [[.6, 0, .6, 0], [-.4, 0, -.4, 0]]),
        ("look_around",  "Look around",   3, True,  "idle",
            [[.5, .3, .5, -.3], [-.5, -.3, -.5, .3], [0, 0, 0, 0]]),
        ("happy_wiggle", "Happy wiggle",  0, True,  "gesture",
            [[1, .6, -1, -.6], [-1, -.6, 1, .6], [1, .6, -1, -.6]]),
        ("curious_lean", "Curious lean",  3, True,  "gesture",
            [[.7, .8, .7, -.8], [0, 0, 0, 0]]),
        ("sleepy_rock",  "Sleepy rock",   4, True,  "idle",
            [[.4, .6, .4, -.6], [-.4, -.6, -.4, .6]]),
        ("little_bow",   "Little bow",    5, False, "gesture",
            [[1, .4, 1, -.4], [0, 0, 0, 0]]),
        ("shiver",       "Shiver",        1, False, "gesture",
            [[.5, 0, -.5, 0], [-.5, 0, .5, 0], [.5, 0, -.5, 0], [-.5, 0, .5, 0]]),
        ("cheer_bounce", "Cheer bounce",  0, False, "gesture",
            [[1, 1, -1, -1], [-1, -1, 1, 1], [1, 1, -1, -1], [0, 0, 0, 0]]),
        # Coordinated multi-joint patterns — these actually move a heavy body
        # (single joints don't). All four hit each keyframe in the same frame.
        ("sync_bounce",  "Bounce (sync)", 0, True,  "gesture",
            [[1, 1, 1, 1], [-1, -1, -1, -1], [1, 1, 1, 1], [-1, -1, -1, -1]]),
        ("foot_flap",    "Foot flap",     0, True,  "gesture",
            [[.4, 1, .4, 1], [-.4, -1, -.4, -1], [.4, 1, .4, 1], [-.4, -1, -.4, -1]]),
        ("waddle",       "Waddle",        0, True,  "idle",
            [[1, .5, -1, -.5], [-1, -.5, 1, .5], [1, .5, -1, -.5], [-1, -.5, 1, .5]]),
        ("wave4",        "Leg wave",      5, False, "gesture",
            [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1], [0, 0, 0, 0]]),
    ]
    library = {}
    for key, name, face, idle, cat, poses in specs:
        frames = [{"angles": list(neutral), "moveMs": 900, "holdMs": 350}]
        for off in poses:
            # Clamp to the travel envelope AND to the 0..180 command range, so a
            # neutral near a servo limit (e.g. a low foot angle) never underflows.
            ang = [round(clampf(neutral[i] + off[i] * a,
                                max(0, neutral[i] - radius),
                                min(180, neutral[i] + radius)), 1)
                   for i in range(4)]
            frames.append({"angles": ang, "moveMs": 700, "holdMs": 200})
        frames.append({"angles": list(neutral), "moveMs": 900, "holdMs": 500})
        library[key] = {"version": 1, "name": name, "mode": "joints",
                        "expression": face, "_category": cat, "_idle": idle,
                        "frames": frames}
    return library


def tilt_movements(tilt_amplitude, cutoff):
    """Target body orientations (pitch,roll degrees), PID-driven."""
    t = min(tilt_amplitude, cutoff, 15)
    # key, name, face, idle-ok, category, [(pitch,roll) targets]
    specs = [
        ("tilt_nod",    "Nod (tilt)",     0, True,  "gesture",
            [(t, 0), (-t * .5, 0), (0, 0)]),
        ("tilt_shake",  "Shake (tilt)",   2, True,  "gesture",
            [(0, t), (0, -t), (0, t), (0, -t), (0, 0)]),
        ("tilt_sway",   "Sway (tilt)",    4, True,  "idle",
            [(0, t * .6), (0, -t * .6), (0, 0)]),
        ("tilt_wobble", "Wobble (tilt)",  3, True,  "idle",
            [(t * .5, t * .5), (-t * .5, t * .5),
             (-t * .5, -t * .5), (t * .5, -t * .5), (0, 0)]),
        ("tilt_bow",    "Bow (tilt)",     5, False, "gesture",
            [(t, 0), (0, 0)]),
        ("tilt_perk",   "Perk up (tilt)", 0, True,  "idle",
            [(-t * .6, 0), (0, 0)]),
    ]
    library = {}
    for key, name, face, idle, cat, targets in specs:
        frames = [{"tilt": [0, 0], "moveMs": 700, "holdMs": 250}]
        for p, r in targets:
            frames.append({"tilt": [round(p, 1), round(r, 1)],
                           "moveMs": 650, "holdMs": 250})
        frames.append({"tilt": [0, 0], "moveMs": 700, "holdMs": 400})
        library[key] = {"version": 1, "name": name, "mode": "tilt",
                        "expression": face, "_category": cat, "_idle": idle,
                        "frames": frames}
    return library


def clampf(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def validate(seq, neutral, radius, cutoff):
    """Mirror the firmware's validSequence so files are known-acceptable."""
    if not seq["frames"] or len(seq["frames"]) > 16:
        raise ValueError(f'{seq["name"]}: 1..16 frames required')
    for f in seq["frames"]:
        if not (100 <= f["moveMs"] <= 5000) or not (0 <= f["holdMs"] <= 5000):
            raise ValueError(f'{seq["name"]}: move/hold time out of range')
        if seq["mode"] == "tilt":
            lim = min(cutoff, 15)
            if len(f["tilt"]) != 2 or any(abs(x) > lim for x in f["tilt"]):
                raise ValueError(f'{seq["name"]}: tilt target beyond +/-{lim}')
        else:
            if len(f["angles"]) != 4:
                raise ValueError(f'{seq["name"]}: need 4 joint angles')
            for i, v in enumerate(f["angles"]):
                if not (0 <= v <= 180):
                    raise ValueError(f'{seq["name"]}: angle out of 0..180')
                if abs(v - neutral[i]) > radius + 1e-6:
                    raise ValueError(f'{seq["name"]}: {JOINT_ORDER[i]} exceeds radius')


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--neutral", nargs=4, type=int, required=True,
                   metavar=("LH", "LF", "RH", "RF"))
    p.add_argument("--radius", type=int, default=30,
                   help="joint travel envelope around neutral (matches /savepose)")
    p.add_argument("--amplitude", type=int, default=18,
                   help="joint-movement size in degrees (clamped to radius)")
    p.add_argument("--tilt-amplitude", type=float, default=6,
                   help="posture-movement target tilt in degrees")
    p.add_argument("--cutoff", type=float, default=15,
                   help="tilt safety cutoff used to clamp posture targets")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if len(args.neutral) != 4 or any(not 3 <= v <= 177 for v in args.neutral):
        p.error("Four idle angles with >=3 degrees of clearance required")

    library = {}
    library.update(joint_movements(args.neutral, args.radius, args.amplitude))
    library.update(tilt_movements(args.tilt_amplitude, args.cutoff))

    args.output.mkdir(parents=True, exist_ok=True)
    files = []
    for key, seq in library.items():
        validate(seq, args.neutral, args.radius, args.cutoff)
        # Strip authoring-only metadata (leading underscore) from the saved file.
        out = {k: v for k, v in seq.items() if not k.startswith("_")}
        (args.output / (key + ".json")).write_text(json.dumps(out, indent=2) + "\n")
        files.append({"file": key + ".json", "name": seq["name"],
                      "mode": seq["mode"], "category": seq["_category"],
                      "idle": seq["_idle"], "expression": seq["expression"]})

    manifest = {
        "version": 1, "neutral": args.neutral, "jointOrder": JOINT_ORDER,
        "radius": args.radius, "amplitude": args.amplitude,
        "tiltAmplitude": args.tilt_amplitude, "cutoff": args.cutoff,
        "physicalValidation": "not tested",
        "purpose": "Motion Studio import + ESP32 slots; idle-tagged for later "
                   "BOB_ROBO_CONVO idle selection (integration deferred)",
        "requirements": [
            "Saved calibrated idle pose (Save Calibrated Position / radius >= amplitude)",
            "Fresh calibrated IMU before playing",
            "Posture (tilt) movements require PID enabled and tuned",
            "Supported/observed trial before trusting any movement",
        ],
        "movements": files,
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    joints = sum(1 for f in files if f["mode"] == "joints")
    tilts = len(files) - joints
    print(f"Wrote {len(files)} movements ({joints} joint, {tilts} tilt) to "
          f"{args.output}; validated offline; no robot requests made")


if __name__ == "__main__":
    main()
