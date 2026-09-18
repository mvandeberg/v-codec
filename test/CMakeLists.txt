include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

set(VCODEC_TEST_FLAGS -Wall -Wextra -Wpedantic)
if(VCODEC_SANITIZE)
    list(APPEND VCODEC_TEST_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# Spec §13.1: test/core must build without any JSON header reachable. We give it an include
# tree that contains only the format-free headers, built from symlinks at configure time, so
# `#include <vcodec/json/...>` from core code or core tests is a build failure.
set(VCODEC_CORE_ONLY_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/core-only-include)
file(MAKE_DIRECTORY ${VCODEC_CORE_ONLY_INCLUDE}/vcodec)
file(GLOB _core_root_headers ${PROJECT_SOURCE_DIR}/include/vcodec/*.hpp)
foreach(h IN LISTS _core_root_headers)
    get_filename_component(name ${h} NAME)
    if(NOT name MATCHES "^(json|cbor|vcodec)\\.hpp$")
        file(CREATE_LINK ${h} ${VCODEC_CORE_ONLY_INCLUDE}/vcodec/${name} SYMBOLIC)
    endif()
endforeach()
file(CREATE_LINK ${PROJECT_SOURCE_DIR}/include/vcodec/core ${VCODEC_CORE_ONLY_INCLUDE}/vcodec/core SYMBOLIC)

function(vcodec_add_test name)
    cmake_parse_arguments(ARG "CORE_ONLY;SPIKE" "" "SOURCES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_compile_features(${name} PRIVATE cxx_std_26)
    target_compile_options(${name} PRIVATE ${VCODEC_TEST_FLAGS}
        $<$<CXX_COMPILER_ID:GNU>:-freflection>
        $<$<CXX_COMPILER_ID:Clang>:-freflection -fexpansion-statements>)
    if(ARG_CORE_ONLY)
        target_include_directories(${name} PRIVATE ${VCODEC_CORE_ONLY_INCLUDE} ${PROJECT_SOURCE_DIR}/test/support)
    else()
        target_link_libraries(${name} PRIVATE vcodec::vcodec)
        target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/test/support)
    endif()
    if(ARG_SPIKE)
        target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
    endif()
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(${name} TEST_PREFIX "${name}.")
endfunction()

add_subdirectory(core)
add_subdirectory(json)
add_subdirectory(compile_fail)
add_subdirectory(roundtrip)
add_subdirectory(conformance)
add_subdirectory(cross)
if(VCODEC_BUILD_CBOR)
    add_subdirectory(cbor)
endif()

# Layering check as a test too, so `ctest` alone catches a violation.
add_test(NAME layering_check
    COMMAND ${CMAKE_COMMAND} -DVCODEC_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include/vcodec
            -P ${PROJECT_SOURCE_DIR}/cmake/LayeringCheck.cmake)
