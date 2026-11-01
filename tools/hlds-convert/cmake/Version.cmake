# Writes the version the converter reports and puts in manifest.bin, from
# `git describe`, as tools/buildid.py does for the ROM. Only when it has changed.
#
#   cmake -DROOT=<repository> -DOUT=<file> -P Version.cmake
set(version "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${ROOT}" OUTPUT_VARIABLE described
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
    if(NOT failed AND described)
        set(version "${described}")
    endif()
endif()
set(text "// Written by cmake/Version.cmake: do not edit.\nnamespace hlds\n{\n    extern const char kConverterVersion[] = \"${version}\";\n}\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT "${old}" STREQUAL "${text}")
    file(WRITE "${OUT}" "${text}")
endif()
