# Registers a CMake config package in the per-user package registry so that a bare
# find_package(<PACKAGE_NAME> CONFIG) resolves it with no CMAKE_PREFIX_PATH and no env var.
#
#   Windows: HKCU\Software\Kitware\CMake\Packages\<PACKAGE_NAME>  (value -> config dir)
#   Unix/macOS: ~/.cmake/packages/<PACKAGE_NAME>/<hash>          (file contents -> config dir)
#
# Usable two ways:
#   - included from install(CODE) with PACKAGE_NAME / CONFIG_DIR already set (local installs)
#   - invoked standalone by an installer's post-install step:
#       cmake -DPACKAGE_NAME=GFX_RELOADED -DCONFIG_DIR=<prefix>/lib/cmake/GFX_RELOADED \
#             -P RegisterCMakePackage.cmake
#
# The entry name is an MD5 of the config dir, so re-registering the same location is idempotent.

if(NOT DEFINED PACKAGE_NAME OR NOT DEFINED CONFIG_DIR)
    message(FATAL_ERROR "RegisterCMakePackage: PACKAGE_NAME and CONFIG_DIR must both be set.")
endif()

file(TO_CMAKE_PATH "${CONFIG_DIR}" CONFIG_DIR)
string(MD5 _entry "${CONFIG_DIR}")

if(CMAKE_HOST_WIN32)
    execute_process(
        COMMAND reg add "HKCU\\Software\\Kitware\\CMake\\Packages\\${PACKAGE_NAME}"
                /v "${_entry}" /t REG_SZ /d "${CONFIG_DIR}" /f
        RESULT_VARIABLE _rc
        OUTPUT_QUIET ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(WARNING "Failed to register ${PACKAGE_NAME} in the CMake user package registry: ${_err}")
    else()
        message(STATUS "Registered ${PACKAGE_NAME} -> ${CONFIG_DIR} (HKCU CMake package registry)")
    endif()
else()
    set(_registry "$ENV{HOME}/.cmake/packages/${PACKAGE_NAME}")
    file(WRITE "${_registry}/${_entry}" "${CONFIG_DIR}\n")
    message(STATUS "Registered ${PACKAGE_NAME} -> ${CONFIG_DIR} (${_registry})")
endif()