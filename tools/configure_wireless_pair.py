"""Generate local ESP-NOW pairing data shared by the two Feather builds."""
import argparse
from pathlib import Path
import re
import secrets


def mac(value):
    if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", value):
        raise argparse.ArgumentTypeError("Use a STA MAC in AA:BB:CC:DD:EE:FF format")
    result = bytes.fromhex(value.replace(":", ""))
    if result[0] & 1 or not any(result):
        raise argparse.ArgumentTypeError("A nonzero unicast STA MAC is required")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", required=True, type=mac)
    parser.add_argument("--body", required=True, type=mac)
    parser.add_argument("--channel", type=int, choices=range(1, 12), default=6)
    args = parser.parse_args()
    if args.camera == args.body:
        parser.error("The two boards must have different MAC addresses")
    target = Path(__file__).resolve().parents[1] / "firmware/shared/WirelessConfig.h"
    lines = ["#pragma once", "#include <stdint.h>", "namespace wirelessConfig {"]
    for name, data in (("CameraMac", args.camera), ("BodyMac", args.body),
                       ("Pmk", secrets.token_bytes(16)), ("Lmk", secrets.token_bytes(16))):
        values = ",".join(f"0x{x:02x}" for x in data)
        lines.append(f"constexpr uint8_t {name}[{len(data)}]={{{values}}};")
    lines += [f"constexpr uint8_t Channel={args.channel};", "}", ""]
    # Avoid accidentally replacing an active pair's keys. Move the file aside to re-pair.
    with target.open("x") as output:
        output.write("\n".join(lines))
    print(f"Created {target}. Build BOTH boards with this file; keep the keys private.")


if __name__ == "__main__":
    main()
