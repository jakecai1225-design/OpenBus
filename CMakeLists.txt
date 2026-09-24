cmake_minimum_required(VERSION 3.21)

project(openbus
    VERSION 0.1.0
    DESCRIPTION "CAN/CAN FD frame analyze, replay, record, trace, graphic desktop app"
    LANGUAGES CXX
)

# ============================================================
#  Fix for MinGW - Disable predefines generation (AutoMoc failure)
# ============================================================
set(CMAKE_MOC_PREDEFS_CMD "")
set(CMAKE_CXX_COMPILE_OPTIONS_PCH "")

# ============================================================
#  C++ standard
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
#  Qt automoc / autouic / autorcc
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ============================================================
#  Build notes
#  - Preferred toolchain: MSYS2 UCRT64 (scripts/setup_msys2.sh, scripts/build.py)
#  - PCH is configured in src/CMakeLists.txt
#  - Use default ld.bfd (best compatibility; no file-lock issues)
#  - Do not use ccache (incompatible with MinGW g++ PCH; silent crashes)
#  - Do not use LLD on Windows (can produce broken binaries / file locks)
#  - Dev profile: -O1 -g1, separate build-dev/ alongside full Debug build/
# ============================================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Using default ld.bfd linker (MSYS2/MinGW)")

    # Dev profile — daily develop: O1 + line-level debug; much smaller/faster link
    set(CMAKE_CXX_FLAGS_DEV "-O1 -g1")

    # thin archive: store object paths only; static-lib repack is milliseconds.
    # Libraries are internal to this build tree (not installed); thin refs stay valid.
    # To revert: remove the two lines below (back to ar qc thick archives).
    #
    # APPEND must also use T: when the object list exceeds the Windows command-line
    # limit, CMake batches archives (CREATE first, then APPEND). Default APPEND is
    # ar q (no T) and fails on existing thin archives with
    # "Cannot convert existing thin library to normal format".
    # qT append is supported by binutils and matches qcT.
    set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> qT <TARGET> <OBJECTS>")

    # Linker notes (do not retry casually):
    #  - ld.bfd (default): only reliable choice here
    #  - ld.gold: MinGW build is ELF-only; PE link fails on --subsystem / auto-image-base
    #  - LLD: previously produced broken executables
endif()

# ============================================================
#  Output directories
# ============================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================
#  Find Qt6
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)
find_package(Qt6 QUIET COMPONENTS LinguistTools)

qt_standard_project_setup()

# ============================================================
#  Third-party deps
# ============================================================
include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)

# ============================================================
#  Subdirectories
# ============================================================
add_subdirectory(src)

# Driver plugins (.odp external drivers -> build/bin/drivers/<id>/)
add_subdirectory(drivers)

# ============================================================
#  Install
# ============================================================
install(TARGETS openbus
    RUNTIME DESTINATION bin
)
