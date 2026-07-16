vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO kuba--/zip
    REF "v${VERSION}"
    SHA512 e35df05d1db4542223f251b052094a8926f1e84a9051db3ff3f60cd0c3af912e0e3053852df8f24eb37b25c0be90afe058c613e9139ccfad0c3ad4d3950c2e70
    HEAD_REF master
    PATCHES
        fix-name-conflict.diff
        disable-werror.patch
)

# Koral overlay: kubazip vendors its own full copy of miniz.h (src/zip.c #includes it), whose
# mz_* symbols (mz_zip_writer_add_file, mz_deflateInit, ...) are ordinary externals. minizip-ng
# -- pulled in transitively via OpenColorIO -- defines some of the very same names, and when
# both end up statically linked into the same binary, whichever of the two object files gets
# pulled in first for an unrelated symbol drags its whole mz_* namespace along, so hiding
# kubazip's own symbol *visibility* (private_extern) does not help: both definitions still land
# in the same image, and the linker still needs exactly one of them, which is a hard error on
# Apple's linker rather than the silent first-wins GNU ld falls back to. kubazip's own public
# API (zip.h / the zip_* names) never references mz_* -- it is purely an internal implementation
# detail of the vendored miniz -- so renaming the whole mz_* prefix here is safe and removes the
# overlap outright, rather than chasing one newly-colliding symbol at a time.
file(READ "${SOURCE_PATH}/src/miniz.h" KORAL_MINIZ_H)
string(REGEX REPLACE "([^A-Za-z0-9_]|^)mz_" "\\1koral_kubazip_mz_" KORAL_MINIZ_H "${KORAL_MINIZ_H}")
file(WRITE "${SOURCE_PATH}/src/miniz.h" "${KORAL_MINIZ_H}")

file(READ "${SOURCE_PATH}/src/zip.c" KORAL_ZIP_C)
string(REGEX REPLACE "([^A-Za-z0-9_]|^)mz_" "\\1koral_kubazip_mz_" KORAL_ZIP_C "${KORAL_ZIP_C}")
file(WRITE "${SOURCE_PATH}/src/zip.c" "${KORAL_ZIP_C}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCMAKE_DISABLE_TESTING=ON
        -DCMAKE_C_VISIBILITY_PRESET=hidden
        -DCMAKE_CXX_VISIBILITY_PRESET=hidden
        -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/zip" PACKAGE_NAME "zip-kuba--")

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/kubazip/zip/zip.h" "#ifndef ZIP_SHARED" "#if 0")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# legacy polyfill
file(INSTALL "${CURRENT_PORT_DIR}/kubazipConfig.cmake" "${CURRENT_PORT_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
