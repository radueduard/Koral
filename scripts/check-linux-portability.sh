#!/usr/bin/env bash
#
# Guards the two portability properties of a shipped Linux build that are invisible until a user on
# other hardware tries to run it — and that a release has silently violated before.
#
#   1. glibc floor. v0.0.3 shipped requiring GLIBC_2.43, because it was built on a CachyOS desktop.
#      Ubuntu 24.04 has 2.39 and Debian 12 has 2.36, so the dynamic linker rejected it everywhere
#      that mattered. Nothing in the build fails when this happens; the artifact is simply dead on
#      arrival.
#
#   2. AVX-512 in un-dispatched code. That same machine has the cachyos-znver4 repos enabled, so
#      its prebuilt static libstdc++ is -march=znver4. Koral's own code was always baseline-clean,
#      but linking stdc++exp for std::stacktrace pulled unguarded AVX-512 into std::to_chars.
#      That is a SIGILL on every Intel consumer CPU through 12th gen.
#
# Both are properties of the *artifact*, not of the source, so they can only be checked here.
#
# Usage: check-linux-portability.sh <path-to-runtime-dir> [max-glibc-version]

set -euo pipefail

root="${1:?usage: check-linux-portability.sh <runtime-dir> [max-glibc]}"
max_glibc="${2:-2.35}"

fail=0

# Binaries to inspect: the engine library, the executable, and anything else we bundle.
mapfile -t targets < <(find "$root" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) | sort)

if [ ${#targets[@]} -eq 0 ]; then
    echo "error: no binaries found under $root" >&2
    exit 1
fi

# Sorts versions and checks the highest required one against the floor. Only glibc's own versioned
# symbols matter here — GLIBCXX is handled by bundling libstdc++ in the runtime archive.
check_glibc() {
    local f="$1"
    local worst
    worst=$(objdump -T "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/GLIBC_//' | sort -V | tail -1)
    [ -z "$worst" ] && return 0

    if [ "$(printf '%s\n%s\n' "$worst" "$max_glibc" | sort -V | tail -1)" != "$max_glibc" ]; then
        echo "  FAIL $(basename "$f"): needs GLIBC_$worst (floor is $max_glibc)"
        return 1
    fi
    echo "  ok   $(basename "$f"): max GLIBC_$worst"
    return 0
}

# AVX-512 is only a problem where it is reached unconditionally. Libraries that select it at
# runtime after a CPUID check — OpenSSL, zlib-ng, OpenColorIO — are fine and genuinely common, so
# matching on the mnemonic alone would be all false positives. The symbols those live in are mostly
# named for the fact, which is what the filter below leans on.
#
# Two exclusions are not self-describing and are worth naming, since they look alarming in a diff:
# OpenColorIO's linear1D<> templates live in a TU compiled for AVX-512 and are reached only through
# OCIO's CPUInfo dispatch, and OpenSSL's iv_len_* helpers are local labels inside its already-
# dispatched AES-GCM paths. Neither is entered without a CPUID check.
#
# What this is really looking for is the libstdc++/libbacktrace case: std::to_chars,
# std::vprint_nonunicode and __glibcxx_backtrace_*, which are called unconditionally and are the
# ones an -march-optimised distro toolchain actually poisons.
check_avx512() {
    local f="$1"
    local hits
    hits=$(objdump -d "$f" 2>/dev/null | awk '
        /^[0-9a-f]+ <.*>:$/ { sym=substr($0, index($0,"<")+1); sub(/>:$/,"",sym); next }
        /\t(kmov[bwdq]|vmovdqu(8|16|32|64)|vpermt2|vpermi2|vpternlog|vpcmpu|vpbroadcastm|vpconflict|vpmadd52|valign[dq]|vpscatter)[ \t]/ { print sym }
    ' | sort -u | grep -vE 'avx512|AVX512|ossl_|aesni_|rsaz_|gcm_|poly1305_|ChaCha20_|adler32_|crc32_|_vaes_|ifma256|vnni|_avx2|_sse|iv_len_|OpenColorIO' || true)

    if [ -n "$hits" ]; then
        echo "  FAIL $(basename "$f"): AVX-512 in symbols with no runtime dispatch:"
        echo "$hits" | sed 's/^/         /'
        return 1
    fi
    echo "  ok   $(basename "$f"): no un-dispatched AVX-512"
    return 0
}

echo "Checking ${#targets[@]} binaries (glibc floor $max_glibc)"
for f in "${targets[@]}"; do
    file "$f" | grep -q 'ELF' || continue
    check_glibc  "$f" || fail=1
    check_avx512 "$f" || fail=1
done

if [ "$fail" -ne 0 ]; then
    echo
    echo "Portability check FAILED. This artifact would not run on the hardware/distros this"
    echo "release targets. See docker/linux-release.Dockerfile."
    exit 1
fi

echo "Portability check passed."
