# §13.5: one .cpp per §10 message, each with inline // expect-error: directives, driven by
# cmake/CompileFailTest.cmake through ctest. The harness asserts failure, matches every
# directive, and enforces the no-cascade guard on every case.
file(GLOB cases CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
set(flags "-std=c++26;-freflection")
foreach(case IN LISTS cases)
    get_filename_component(name ${case} NAME_WE)
    add_test(NAME compile_fail.${name}
        COMMAND ${CMAKE_COMMAND}
            -DSRC=${case}
            -DCXX=${CMAKE_CXX_COMPILER}
            "-DFLAGS=${flags}"
            "-DINCLUDE=${PROJECT_SOURCE_DIR}/include"
            -P ${PROJECT_SOURCE_DIR}/cmake/CompileFailTest.cmake)
endforeach()

# The harness must also notice a case that compiles: this "case" is expected to be reported
# as a harness failure, so the test inverts the result.
add_test(NAME compile_fail.harness_detects_success
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/should_compile/compiles.cpp.in
        -DCXX=${CMAKE_CXX_COMPILER}
        "-DFLAGS=${flags}"
        "-DINCLUDE=${PROJECT_SOURCE_DIR}/include"
        -P ${PROJECT_SOURCE_DIR}/cmake/CompileFailTest.cmake)
set_tests_properties(compile_fail.harness_detects_success PROPERTIES WILL_FAIL TRUE)
