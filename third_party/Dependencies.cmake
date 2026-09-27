# ============================================================
#  openbus third-party CMake targets
#  Included from top-level CMakeLists.txt
# ============================================================

# ---- qcustomplot (required: graphic + I/O Graph) ----
if(NOT TARGET qcustomplot)
    set(_QCP_DIR "${CMAKE_SOURCE_DIR}/third_party/qcustomplot")
    if(NOT EXISTS "${_QCP_DIR}/qcustomplot.cpp")
        message(FATAL_ERROR "qcustomplot sources missing under third_party/qcustomplot/")
    endif()
    add_library(qcustomplot STATIC
        "${_QCP_DIR}/qcustomplot.cpp"
        "${_QCP_DIR}/qcustomplot.h"
    )
    target_include_directories(qcustomplot PUBLIC "${_QCP_DIR}")
    target_link_libraries(qcustomplot PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::PrintSupport
    )
    # Silence noisy Qt deprecations inside vendor sources
    target_compile_options(qcustomplot PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wno-deprecated-declarations>
    )
    set_target_properties(qcustomplot PROPERTIES
        AUTOMOC ON
        POSITION_INDEPENDENT_CODE ON
    )
    message(STATUS "qcustomplot: ${_QCP_DIR}")
endif()

# ---- pugixml (optional: ARXML importer) ----
# Expect real sources: pugixml.cpp + pugixml.hpp (not the stub wrapper).
# When only the stub is present, skip the target so the app still builds
# without ARXML XML parsing at runtime.
set(_PUGI_DIR "${CMAKE_SOURCE_DIR}/third_party/pugixml")
if(NOT TARGET pugixml
   AND EXISTS "${_PUGI_DIR}/pugixml.cpp"
   AND EXISTS "${_PUGI_DIR}/pugixml.hpp")
    file(READ "${_PUGI_DIR}/pugixml.hpp" _pugi_hdr LIMIT 200)
    if(_pugi_hdr MATCHES "PUGIXML_INSTALLED|pugixml header wrapper")
        message(STATUS "pugixml: stub headers only — download real sources to enable ARXML")
    else()
        add_library(pugixml STATIC
            "${_PUGI_DIR}/pugixml.cpp"
            "${_PUGI_DIR}/pugixml.hpp"
            "${_PUGI_DIR}/pugiconfig.hpp"
        )
        target_include_directories(pugixml PUBLIC "${_PUGI_DIR}")
        target_compile_features(pugixml PUBLIC cxx_std_17)
        set_target_properties(pugixml PROPERTIES POSITION_INDEPENDENT_CODE ON)
        message(STATUS "pugixml: ${_PUGI_DIR}")
    endif()
endif()

# ---- vector_blf (optional: BLF log I/O) ----
set(_VBLF_DIR "${CMAKE_SOURCE_DIR}/third_party/vector_blf")
if(NOT TARGET vector_blf AND EXISTS "${_VBLF_DIR}/CMakeLists.txt")
    set(OPTION_RUN_DOXYGEN OFF CACHE BOOL "" FORCE)
    set(OPTION_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OPTION_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${_VBLF_DIR}" "${CMAKE_BINARY_DIR}/third_party/vector_blf")
    message(STATUS "vector_blf: ${_VBLF_DIR}")
elseif(NOT TARGET vector_blf)
    message(STATUS "vector_blf: not present — BLF support disabled")
endif()
