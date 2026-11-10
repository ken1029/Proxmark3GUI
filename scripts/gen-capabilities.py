#!/usr/bin/env python3
"""Generate the GUI capability manifest from a firmware build configuration.

Parses "Included options:" printed by common_arm/Makefile.hal, so the manifest
always describes the same defines that produced the firmware image.
"""

import argparse
import json
import os
import re
import subprocess
import sys

FEATURE_MAP = {
    "WITH_LF": "lf",
    "WITH_HITAG": "hitag",
    "WITH_EM4x50": "em4x50",
    "WITH_EM4x70": "em4x70",
    "WITH_ZX8211": "zx8211",
    "WITH_GENERAL_HF": "hf",
    "WITH_ISO15693": "iso15693",
    "WITH_LEGICRF": "legicrf",
    "WITH_ISO14443b": "iso14443b",
    "WITH_ISO14443a": "iso14443a",
    "WITH_DESFIRE_SIM": "desfire_sim",
    "WITH_ICLASS": "iclass",
    "WITH_SEOS": "seos",
    "WITH_FELICA": "felica",
    "WITH_NFCBARCODE": "nfcbarcode",
    "WITH_HFSNIFF": "hfsniff",
    "WITH_HFPLOT": "hfplot",
    "WITH_COMPRESSION": "compression",
}


def make_info(src, make_args):
    cmd = ["make", "-C", src, "-n", "recovery"] + make_args
    proc = subprocess.run(cmd, capture_output=True, text=True)
    text = proc.stdout + proc.stderr
    options = ""
    version = ""
    for line in text.splitlines():
        m = re.match(r"\s*Included options:\s*(.*)$", line)
        if m:
            options = m.group(1).strip()
        m = re.match(r"\s*Version info:\s*(.*)$", line)
        if m:
            version = m.group(1).strip()
    if not options:
        sys.stderr.write(text)
        raise SystemExit("could not read 'Included options' from make output")
    return options.split(), version


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", required=True, help="proxmark3 source directory")
    parser.add_argument("--output", required=True)
    parser.add_argument("--commit", default="")
    parser.add_argument("--version", default="v4.23346")
    parser.add_argument("--platform", default="PM3GENERIC")
    parser.add_argument("--platform-size", type=int, default=256)
    parser.add_argument("--led-order", default="PM3EASY")
    parser.add_argument("--recovery", default="", help="recovery.bin to record its size")
    parser.add_argument("--skip", default="", help="comma separated SKIP_* names")
    args = parser.parse_args()

    make_args = [
        "PLATFORM=" + args.platform,
        "PLATFORM_SIZE=" + str(args.platform_size),
        "LED_ORDER=" + args.led_order,
        "STANDALONE=",
    ]
    for item in [x for x in args.skip.split(",") if x]:
        make_args.append(item + "=1")

    defines, make_version = make_info(args.src, make_args)
    # Makefile.hal prints PLATFORM_DEFS with "-DWITH_" already stripped
    present = set(d[5:] if d.startswith("WITH_") else d for d in defines)
    features = {}
    for define, feature in FEATURE_MAP.items():
        name = define[5:] if define.startswith("WITH_") else define
        features[feature] = name in present
    features["standalone"] = any(d.startswith("STANDALONE_") for d in defines)

    removed = sorted(k.upper() for k, v in features.items() if not v)
    manifest = {
        "manifest_version": 1,
        "firmware": {
            "version": args.version or make_version or "v4.23346",
            "commit": args.commit,
            "platform": args.platform,
            "platform_size": args.platform_size,
            "led_order": args.led_order,
        },
        "features": features,
        "removed": removed,
        "defines": defines,
    }

    if args.recovery and os.path.isfile(args.recovery):
        size = os.path.getsize(args.recovery)
        manifest["firmware"]["recovery_bytes"] = size
        manifest["firmware"]["recovery_max_bytes"] = args.platform_size * 1024
        manifest["firmware"]["recovery_free_bytes"] = args.platform_size * 1024 - size

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    print("wrote", args.output)
    print("removed:", ", ".join(removed) if removed else "(none)")


if __name__ == "__main__":
    main()
