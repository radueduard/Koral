# arm64-osx, with a deployment target.
#
# Identical to vcpkg's built-in arm64-osx apart from the last line, which is the entire reason this
# file exists.
#
# CMAKE_OSX_DEPLOYMENT_TARGET in our own CMakeLists only governs Koral's own binaries. vcpkg builds
# its ports in separate CMake invocations that never see it, and takes its deployment target from
# this triplet variable instead. Without it the ports inherit the build machine's OS, and the
# artifact is only as portable as the newest thing it links:
#
#   libKoral.dylib               minos 13.3   <- ours, correct
#   libslang-compiler.dylib      minos 15.0   <- vcpkg
#   libvulkan.1.4.335.dylib      minos 26.0   <- vcpkg
#
# A runtime archive bundling all three needs macOS 26 no matter what our own library says, and
# nothing in the build reports this — the job is green and the artifact is unusable.
#
# Keep this value in step with CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt. 13.3 is not arbitrary:
# it is the lowest version whose libc++ has the floating-point std::to_chars that std::format needs.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.3)
