# Build environment for the shipped Linux x64 release.
#
# WHY THIS EXISTS
#
# The Linux release used to be built directly on the self-hosted runner, which is radue's CachyOS
# desktop. Two things came out of that, and both made the artifact unusable for essentially
# everyone:
#
#   1. glibc. The v0.0.3 artifact required GLIBC_2.43 and GLIBCXX_3.4.35, because that is what the
#      build machine has. Ubuntu 24.04 ships glibc 2.39, Debian 12 ships 2.36, Ubuntu 22.04 ships
#      2.35 — so the dynamic linker rejected the library outright on every mainstream distro. The
#      binary never got far enough to run any code.
#
#   2. AVX-512. That machine has the `cachyos-znver4` repositories enabled, so its *prebuilt static*
#      libstdc++ archives are compiled -march=znver4. Koral's own code is baseline x86-64 (GCC's
#      default) and was always clean, but linking stdc++exp for std::stacktrace pulled unguarded
#      AVX-512 into std::to_chars and std::print. That is a SIGILL on any CPU without AVX-512,
#      which includes every Intel consumer part through 12th gen.
#
# Both are the same root cause — building against a bleeding-edge, CPU-optimised system toolchain —
# and both are fixed the same way: build against a stock, old-glibc toolchain instead.
#
# CHOICE OF BASE
#
# Ubuntu 22.04 (glibc 2.35, GLIBCXX from the GCC we install below). 22.04 is supported to 2027 and
# is the oldest base still worth targeting; anything older starts fighting vcpkg over Wayland and
# X11 protocol versions. glibc 2.35 covers Ubuntu 22.04+, Debian 12+, Fedora 36+, and current
# Arch/CachyOS — which is the realistic install base for someone on the Intel-8th-gen-and-up
# hardware floor this release targets.
#
# GCC 14 comes from the Ubuntu toolchain PPA because Koral is C++23 (std::expected, std::print,
# std::stacktrace) and jammy's stock GCC 12 cannot build it. The PPA's GCC is built for generic
# x86-64, which is precisely the property the CachyOS toolchain lacked.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# GCC 14 for C++23. software-properties-common is only needed to add the PPA and is removed with
# the rest of the apt lists at the end of the layer.
RUN apt-get update && apt-get install -y --no-install-recommends \
        software-properties-common ca-certificates gpg-agent \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update && apt-get install -y --no-install-recommends \
        gcc-14 g++-14 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && rm -rf /var/lib/apt/lists/*

# Build tooling. CMake comes from pip rather than jammy's apt, which has 3.22 — below the 3.28
# this project requires.
#
# Two things here are load-bearing and easy to lose:
#
#   make. Koral itself builds with Ninja, so it is tempting to install only ninja-build — but vcpkg
#   ports do not all use CMake. OpenSSL drives its own Configure/make build and fails the whole
#   dependency install with "Could not find make" without it.
#
#   linux-libc-dev. OpenSSL also wants the kernel headers, and says so as a warning immediately
#   before failing for the unrelated reason above, which makes it easy to fix one and not the other.
#
# CMake is held below 4.x deliberately. CMake 4 dropped compatibility with
# cmake_minimum_required(<3.5), which a good number of vcpkg ports still declare; picking up 4.x
# here would trade this failure for a longer, less obvious tail of port build failures.
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl zip unzip tar git pkg-config ninja-build make python3 python3-pip \
        linux-libc-dev libc6-dev \
        autoconf automake libtool m4 bison flex nasm ccache \
    && pip3 install --no-cache-dir "cmake>=3.28,<4" \
    && rm -rf /var/lib/apt/lists/*

# The X11/Wayland development headers GLFW, GLEW and the Vulkan loader build against. Same list the
# workflow used to apt-install on hosted runners — it lives here now, so the build environment is
# described in one place instead of being half container and half YAML.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        libxext-dev libxfixes-dev libxrender-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libgl1-mesa-dev libglu1-mesa-dev \
        libltdl-dev \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-14 \
    CXX=g++-14 \
    VCPKG_FORCE_SYSTEM_BINARIES=1
