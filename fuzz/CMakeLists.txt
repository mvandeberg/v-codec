# §13.4 fuzzing. Harnesses expose LLVMFuzzerTestOneInput. With Clang they build against
# libFuzzer; with GCC (which has no libFuzzer) they build against fuzz/standalone_main.cpp, a
# driver that replays the seed corpus and mutates it for a time budget. Both build with
# ASan+UBSan.
set(VCODEC_FUZZ_SECONDS 60 CACHE STRING "seconds per harness for the ctest fuzz run")

set(fuzz_flags -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(fuzz_engine_flags -fsanitize=fuzzer)
    set(fuzz_driver)
else()
    set(fuzz_engine_flags)
    set(fuzz_driver ${CMAKE_CURRENT_SOURCE_DIR}/standalone_main.cpp)
endif()

function(vcodec_add_fuzzer name)
    add_executable(${name} ${name}.cpp ${fuzz_driver})
    target_compile_features(${name} PRIVATE cxx_std_26)
    target_compile_options(${name} PRIVATE ${fuzz_flags} ${fuzz_engine_flags}
        $<$<CXX_COMPILER_ID:GNU>:-freflection>
        $<$<CXX_COMPILER_ID:Clang>:-freflection -fexpansion-statements>)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined ${fuzz_engine_flags})
    target_link_libraries(${name} PRIVATE vcodec::vcodec)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/test/support)
    add_test(NAME fuzz.${name}
        COMMAND ${name} -max_total_time=${VCODEC_FUZZ_SECONDS} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
endfunction()

vcodec_add_fuzzer(fuzz_skip)
vcodec_add_fuzzer(fuzz_typed)
vcodec_add_fuzzer(fuzz_roundtrip)
if(VCODEC_BUILD_CBOR)
    vcodec_add_fuzzer(fuzz_cbor_skip)
    vcodec_add_fuzzer(fuzz_cbor_typed)
    vcodec_add_fuzzer(fuzz_cbor_roundtrip)
    vcodec_add_fuzzer(fuzz_cbor_deterministic)
endif()
