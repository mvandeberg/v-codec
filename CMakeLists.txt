cmake_minimum_required(VERSION 3.28)
project(vcodec VERSION 0.1.0 LANGUAGES CXX)

add_library(vcodec INTERFACE)
add_library(vcodec::vcodec ALIAS vcodec)
target_include_directories(vcodec INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(vcodec INTERFACE cxx_std_26)
target_compile_options(vcodec INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-freflection>
    $<$<CXX_COMPILER_ID:Clang>:-freflection -fexpansion-statements>)

option(VCODEC_BUILD_TESTS      "Build tests"       ${PROJECT_IS_TOP_LEVEL})
option(VCODEC_BUILD_BENCHMARKS "Build benchmarks"  OFF)
option(VCODEC_BUILD_FUZZERS    "Build fuzzers"     OFF)
option(VCODEC_BUILD_CBOR       "Include the CBOR backend in tests and benchmarks" ON)
option(VCODEC_SANITIZE         "Build tests with ASan+UBSan" OFF)

# Structural rule 1: core/ must not include json/, cbor/ or spike/. Checked at configure
# time and again as a test, so a violation is a build failure, not a review comment.
include(cmake/LayeringCheck.cmake)
vcodec_layering_check("${CMAKE_CURRENT_SOURCE_DIR}/include/vcodec")

if(VCODEC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
if(VCODEC_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
if(VCODEC_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

# Install / export
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
install(TARGETS vcodec EXPORT vcodec-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT vcodec-targets NAMESPACE vcodec:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vcodec)
configure_package_config_file(cmake/vcodec-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vcodec-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vcodec)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/vcodec-config-version.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vcodec-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/vcodec-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vcodec)
