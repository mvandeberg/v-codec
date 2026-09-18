# Benchmarks (spec §13.7): vcodec against Glaze, nlohmann/json and simdjson on a shared
# corpus, plus the compile-time budget test. Everything here is opt-in via
# VCODEC_BUILD_BENCHMARKS and fetched with FetchContent.
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

FetchContent_MakeAvailable(benchmark glaze nlohmann_json simdjson)

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

# Compile-time budget (spec §13.7, §14 item 12). compile_time.cpp is also built as an
# ordinary executable so the code it measures is known to link and run.
add_executable(bench_compile_time compile_time.cpp)
target_compile_features(bench_compile_time PRIVATE cxx_std_26)
target_compile_options(bench_compile_time PRIVATE ${VCODEC_BENCH_FLAGS})
target_link_libraries(bench_compile_time PRIVATE vcodec::vcodec)

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
