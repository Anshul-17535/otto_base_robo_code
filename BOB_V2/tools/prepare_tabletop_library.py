#!/usr/bin/env python3
"""Generate small, unverified expression motions; never contacts or moves Bob."""
import argparse
import json
from pathlib import Path


def build_library(neutral):
    if len(neutral) != 4 or any(not 3 <= v <= 177 for v in neutral):
        raise ValueError("Four idle angles with at least 3 degrees of clearance required")
    patterns = [
        ("idle_hold", "Idle hold", 6, [[0, 0, 0, 0]]),
        ("attentive", "Attentive", 6, [[1, 0, 1, 0]]),
        ("little_nod", "Little nod", 0, [[1, 0, 1, 0], [-1, 0, -1, 0]]),
        ("curious", "Curious", 3, [[0, 1, 0, -1], [0, -1, 0, 1]]),
        ("happy_wiggle", "Happy wiggle", 0, [[1, 1, -1, -1], [-1, -1, 1, 1]]),
        ("sleepy", "Sleepy rock", 4, [[0, 1, 0, -1], [0, 0, 0, 0]]),
    ]
    library = {}
    for key, name, face, offsets in patterns:
        frames = [{"angles": list(neutral), "moveMs": 1600, "holdMs": 500}]
        frames += [{"angles": [v + d for v, d in zip(neutral, offset)],
                    "moveMs": 1600, "holdMs": 350} for offset in offsets]
        frames.append({"angles": list(neutral), "moveMs": 1600, "holdMs": 700})
        library[key] = {"version": 1, "name": name, "expression": face, "frames": frames}
    return library


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neutral", nargs=4, type=int, required=True,
                        metavar=("LH", "LF", "RH", "RF"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    library = build_library(args.neutral)
    args.output.mkdir(parents=True, exist_ok=True)
    for key, sequence in library.items():
        (args.output / (key + ".json")).write_text(json.dumps(sequence, indent=2) + "\n")
    manifest = {
        "version": 1, "neutral": args.neutral, "jointOrder": ["LH", "LF", "RH", "RF"],
        "maximumOffsetDegrees": 1, "physicalValidation": "not tested",
        "purpose": "Future BOB_ROBO_CONVO idle and expression selection; integration deferred",
        "files": [key + ".json" for key in library],
        "requirements": ["Motion API v2 and captured custom idle", "Calibrated fresh IMU",
                         "Supported observed trial before use", "PID off; no forward walking"],
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {len(library)} untested movements to {args.output}; no robot requests made")


if __name__ == "__main__":
    main()
