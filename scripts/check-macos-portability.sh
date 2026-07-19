#!/usr/bin/env bash
#
# The macOS counterpart to check-linux-portability.sh, and it exists for the same reason: a green
# build says nothing about whether the artifact runs anywhere but the machine that produced it.
#
# Two real failures motivated this, both of which shipped a passing job:
#
#   1. No deployment target at all. CMAKE_OSX_DEPLOYMENT_TARGET was set *after* project(), where
#      CMake ignores it, so the library declared minos 26.0 -- the runner's own OS.
#
#   2. A deployment target that covered only our own binaries. vcpkg builds each port in a separate
#      CMake invocation that never sees CMAKE_OSX_DEPLOYMENT_TARGET, so libKoral.dylib correctly
#      said 13.3 while the libvulkan and libslang dylibs bundled beside it said 26.0 and 15.0. The
#      archive as a whole still required macOS 26.
#
# The second is the reason this checks *every* Mach-O in the tree rather than just ours: the floor
# of a bundle is the highest floor in it.
#
# Usage: check-macos-portability.sh <path-to-runtime-dir> [max-minos]

set -euo pipefail

root="${1:?usage: check-macos-portability.sh <runtime-dir> [max-minos]}"
max_minos="${2:-13.3}"

# Deliberately not otool/vtool: this has to be runnable off a Mac (it is developed and tested on
# Linux), so it parses LC_BUILD_VERSION / LC_VERSION_MIN_MACOSX out of the Mach-O header directly.
python3 - "$root" "$max_minos" <<'PY'
import struct, sys, os

root, max_minos = sys.argv[1], sys.argv[2]

def ver_tuple(s):
    parts = [int(x) for x in s.split('.')]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts)

MAGICS = {0xfeedfacf: '<', 0xcffaedfe: '>'}   # 64-bit little/big endian

def minos_of(path):
    with open(path, 'rb') as fh:
        head = fh.read(32)
        if len(head) < 32:
            return None
        magic = struct.unpack('<I', head[:4])[0]
        if magic not in MAGICS:
            return None            # not a thin 64-bit Mach-O (fat archives are not produced here)
        data = head + fh.read()
    ncmds = struct.unpack_from('<I', data, 16)[0]
    off, best = 32, None
    for _ in range(ncmds):
        if off + 8 > len(data):
            break
        cmd, sz = struct.unpack_from('<II', data, off)
        if sz == 0:
            break
        if cmd == 0x32 and off + 24 <= len(data):        # LC_BUILD_VERSION
            minos = struct.unpack_from('<I', data, off + 12)[0]
            best = f"{minos >> 16}.{(minos >> 8) & 0xff}.{minos & 0xff}"
        elif cmd == 0x24 and off + 16 <= len(data):      # LC_VERSION_MIN_MACOSX (older linkers)
            v = struct.unpack_from('<I', data, off + 8)[0]
            best = f"{v >> 16}.{(v >> 8) & 0xff}.{v & 0xff}"
        off += sz
    return best

limit = ver_tuple(max_minos)
checked = failed = 0

for dirpath, _, names in os.walk(root):
    for name in sorted(names):
        p = os.path.join(dirpath, name)
        if os.path.islink(p):
            continue
        try:
            m = minos_of(p)
        except Exception:
            continue
        if m is None:
            continue
        checked += 1
        if ver_tuple(m) > limit:
            print(f"  FAIL {name}: built for macOS {m} (floor is {max_minos})")
            failed += 1
        else:
            print(f"  ok   {name}: macOS {m}")

if checked == 0:
    print(f"error: no Mach-O binaries found under {root}", file=sys.stderr)
    sys.exit(1)

print()
if failed:
    print(f"Portability check FAILED: {failed} of {checked} binaries need a newer macOS than the")
    print(f"{max_minos} floor. A bundle is only as portable as the highest floor in it.")
    print("If the offenders are vcpkg-built dylibs, VCPKG_OSX_DEPLOYMENT_TARGET in")
    print("vcpkg-overlay-triplets/arm64-osx.cmake is the knob, not CMAKE_OSX_DEPLOYMENT_TARGET.")
    sys.exit(1)

print(f"Portability check passed ({checked} binaries, all <= macOS {max_minos}).")
PY
