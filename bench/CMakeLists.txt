# Benchmarks (spec §13.7): vcodec against Glaze, nlohmann/json and simdjson on a shared JSON
# corpus, against Glaze and QCBOR on the CBOR corpus, plus the compile-time budget tests.
# Everything here is opt-in via VCODEC_BUILD_BENCHMARKS and fetched with FetchContent.
include(FetchContent)

set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS       OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.4
    GIT_SHALLOW    TRUE)

set(glaze_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG        v5.5.4
    GIT_SHALLOW    TRUE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE)

set(SIMDJSON_JUST_LIBRARY     ON  CACHE BOOL "" FORCE)
set(SIMDJSON_BUILD_STATIC_LIB ON  CACHE BOOL "" FORCE)
set(SIMDJSON_DEVELOPER_MODE   OFF CACHE BOOL "" FORCE)
set(SIMDJSON_ENABLE_THREADS   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.13.0
    GIT_SHALLOW    TRUE)

# CBOR comparators (spec v0.2 §13.7). Glaze grew its CBOR backend after v5.5.4, the version
# the JSON tables in BASELINE.md were recorded against, so the CBOR benchmarks take a second,
# download-only checkout of Glaze v8.4.0 and expose its headers as an INTERFACE target.
# SOURCE_SUBDIR names a directory with no CMakeLists.txt, which FetchContent documents as
# "populate, but do not add_subdirectory"; the two Glaze versions never meet in one TU.
FetchContent_Declare(glaze_cbor
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG        v8.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-no-subdirectory)

# QCBOR is a C library; its CMakeLists builds just the static library when tests are off.
set(BUILD_QCBOR_TEST OFF CACHE STRING "" FORCE)
set(BUILD_QCBOR_WARN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(qcbor
    GIT_REPOSITORY https://github.com/laurencelundblade/QCBOR.git
    GIT_TAG        v1.5.1
    GIT_SHALLOW    TRUE)

set(_vcodec_bench_deps benchmark glaze nlohmann_json simdjson)
if(VCODEC_BUILD_CBOR)
    list(APPEND _vcodec_bench_deps glaze_cbor qcbor)
endif()
FetchContent_MakeAvailable(${_vcodec_bench_deps})

if(VCODEC_BUILD_CBOR)
    add_library(glaze_cbor INTERFACE)
    target_include_directories(glaze_cbor SYSTEM INTERFACE ${glaze_cbor_SOURCE_DIR}/include)
endif()

set(VCODEC_BENCH_FLAGS -O2 $<$<CXX_COMPILER_ID:GNU>:-freflection>
                           $<$<CXX_COMPILER_ID:Clang>:-freflection -fexpansion-statements>)

function(vcodec_add_bench name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_26)
    target_compile_options(${name} PRIVATE ${VCODEC_BENCH_FLAGS})
    target_link_libraries(${name} PRIVATE
        vcodec::vcodec
        benchmark::benchmark_main
        glaze::glaze
        nlohmann_json::nlohmann_json
        simdjson::simdjson)
endfunction()

vcodec_add_bench(bench_encode encode.cpp)
vcodec_add_bench(bench_decode decode.cpp)

# CBOR: shape × {vcodec deterministic, vcodec non-deterministic, Glaze, QCBOR}. nlohmann is
# linked only because cbor_corpus.hpp reuses corpus.hpp, which carries the nlohmann adapters.
function(vcodec_add_cbor_bench name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_26)
    target_compile_options(${name} PRIVATE ${VCODEC_BENCH_FLAGS})
    target_link_libraries(${name} PRIVATE
        vcodec::vcodec
        benchmark::benchmark_main
        glaze_cbor
        qcbor
        nlohmann_json::nlohmann_json)
endfunction()

if(VCODEC_BUILD_CBOR)
    vcodec_add_cbor_bench(bench_cbor_encode cbor_encode.cpp)
    vcodec_add_cbor_bench(bench_cbor_decode cbor_decode.cpp)
endif()

# Compile-time budget (spec §13.7, §14 item 12). compile_time.cpp is also built as an
# ordinary executable so the code it measures is known to link and run.
add_executable(bench_compile_time compile_time.cpp)
target_compile_features(bench_compile_time PRIVATE cxx_std_26)
target_compile_options(bench_compile_time PRIVATE ${VCODEC_BENCH_FLAGS})
target_link_libraries(bench_compile_time PRIVATE vcodec::vcodec)

# The CBOR TU (spec v0.2 §13.7): the same structs through vcodec::cbor, with its own baseline so
# the JSON budget keeps measuring only JSON — that is the check that core/ changes did not tax it.
if(VCODEC_BUILD_CBOR)
    add_executable(bench_compile_time_cbor compile_time_cbor.cpp)
    target_compile_features(bench_compile_time_cbor PRIVATE cxx_std_26)
    target_compile_options(bench_compile_time_cbor PRIVATE ${VCODEC_BENCH_FLAGS})
    target_link_libraries(bench_compile_time_cbor PRIVATE vcodec::vcodec)
endif()

# The root CMakeLists only calls enable_testing() when VCODEC_BUILD_TESTS is on; a
# benchmark-only configuration still needs the budget test registered.
enable_testing()

# The budget test re-compiles compile_time.cpp N times with the same compiler and the
# flags below, takes the median wall-clock time and compares it to the recorded baseline.
add_test(NAME compile_time_budget
    COMMAND ${CMAKE_COMMAND}
        -DCXX=${CMAKE_CXX_COMPILER}
        "-DFLAGS=-std=c++26;-freflection;-O2;-I${PROJECT_SOURCE_DIR}/include"
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compile_time.cpp
        -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/compile_time_baseline.txt
        -DRUNS=3
        -DTOLERANCE=0.15
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CompileTimeBudget.cmake)

if(VCODEC_BUILD_CBOR)
    add_test(NAME compile_time_budget_cbor
        COMMAND ${CMAKE_COMMAND}
            -DCXX=${CMAKE_CXX_COMPILER}
            "-DFLAGS=-std=c++26;-freflection;-O2;-I${PROJECT_SOURCE_DIR}/include"
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compile_time_cbor.cpp
            -DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/compile_time_cbor_baseline.txt
            -DRUNS=3
            -DTOLERANCE=0.15
            -P ${CMAKE_CURRENT_SOURCE_DIR}/CompileTimeBudget.cmake)
endif()
