"""Check an offline 2,208-present renderer regression log."""
import argparse
from pathlib import Path
import re


def check(logfile, backend, slots):
    log = logfile.read_text(errors="replace")
    required = [
        r"^frames=2208 hash=[0-9a-f]+",
        rf"^arceus-rt: {slots} material texture slots",
        r"debug: vk_validation=1", r"vulkan validation:", r"retainedPbrHits=[1-9]",
        r"shader_cache_world_pt_[a-f0-9]{16}",
    ]
    if backend == "RR":
        required += [r"arceus-rr: Ray Reconstruction created 1920x1080 -> 3840x2160",
                     r"arceus-rr: evaluated raw radiance at present 2160"]
    else:
        actual = r"FSR 3\.1\.6" if backend == "FSR" else "DLSS"
        required += ["temporal radiance filter ready",
                     rf"arceus-upscaler: {actual} pre-HUD 1920x1080 -> 3840x2160 frame=1 "]
        if backend != "FSR":
            required.append("ModelPreset, Value: 13, Requested: Yes, Applied: Yes")
        if backend == "RR-fallback":
            required.append("feature creation failed")
    for signal in required:
        if not re.search(signal, log, re.MULTILINE):
            raise ValueError(f"{logfile}: missing {signal}")
    known = ("has an Output value declared at Location", "and this write is unused",
             "which is the duplicate_message_limit value")
    errors = re.compile(r"THREADING ERROR|VUID-[A-Za-z]|device lost|vendor dispatch failed|"
                        r"undefined values|validation layer.*not (found|available)", re.I)
    for line in log.splitlines():
        if errors.search(line) or ("vulkan validation:" in line and not any(s in line for s in known)):
            raise ValueError(f"{logfile}: {line}")
    print(f"{logfile.name} passed ({backend}, {slots} slots, 2208 presents)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("backend", choices=("FSR", "DLSS", "RR", "RR-fallback"))
    parser.add_argument("slots", type=int, choices=(16, 192))
    args = parser.parse_args()
    check(args.log, args.backend, args.slots)
