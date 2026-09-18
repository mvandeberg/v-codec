# Compile-fail harness (spec §13.5). Run in script mode by ctest:
#
#   cmake -DSRC=<case.cpp> -DCXX=<compiler> -DFLAGS=<flags;list> -DINCLUDE=<dir>[;<dir>]
#         -P CompileFailTest.cmake
#
# The case must fail to compile, every `// expect-error: <text>` directive must appear in
# stderr, every `// expect-not: <text>` must not. The guard shared by all cases enforces the
# §10 requirement that instantiation never fails inside the traversal: exactly one
# `static assertion failed` and at most one instantiation frame, naming a vcodec::json entry
# point. A case that fails for the wrong reason is a test failure.

if(NOT SRC OR NOT CXX)
    message(FATAL_ERROR "usage: cmake -DSRC=... -DCXX=... [-DFLAGS=...] [-DINCLUDE=...] -P CompileFailTest.cmake")
endif()

file(STRINGS "${SRC}" expects REGEX "^// expect-error: ")
file(STRINGS "${SRC}" rejects REGEX "^// expect-not: ")
if(NOT expects)
    message(FATAL_ERROR "${SRC}: no // expect-error: directive")
endif()

set(inc_flags)
foreach(dir IN LISTS INCLUDE)
    list(APPEND inc_flags "-I${dir}")
endforeach()

execute_process(
    COMMAND ${CXX} ${FLAGS} ${inc_flags} -fsyntax-only -fdiagnostics-plain-output "${SRC}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)

if(rc EQUAL 0)
    message(FATAL_ERROR "${SRC}: compiled successfully, expected failure")
endif()

set(failed FALSE)
foreach(line IN LISTS expects)
    string(REGEX REPLACE "^// expect-error: " "" text "${line}")
    string(FIND "${err}" "${text}" pos)
    if(pos EQUAL -1)
        message(SEND_ERROR "${SRC}: expected diagnostic text not found: '${text}'")
        set(failed TRUE)
    endif()
endforeach()
foreach(line IN LISTS rejects)
    string(REGEX REPLACE "^// expect-not: " "" text "${line}")
    string(FIND "${err}" "${text}" pos)
    if(NOT pos EQUAL -1)
        message(SEND_ERROR "${SRC}: forbidden diagnostic text present: '${text}'")
        set(failed TRUE)
    endif()
endforeach()

# Guard: one static assertion, one instantiation frame, no template wall.
string(REGEX MATCHALL "static assertion failed" asserts "${err}")
list(LENGTH asserts n_asserts)
string(REGEX MATCHALL "In instantiation of" frames "${err}")
list(LENGTH frames n_frames)
string(REGEX MATCHALL "required from" requireds "${err}")
list(LENGTH requireds n_required)

set(guard_exempt FALSE)
file(STRINGS "${SRC}" exempt REGEX "^// guard-exempt")
if(exempt)
    set(guard_exempt TRUE)
endif()

if(NOT guard_exempt)
    if(NOT n_asserts EQUAL 1)
        message(SEND_ERROR "${SRC}: expected exactly one 'static assertion failed', found ${n_asserts}")
        set(failed TRUE)
    endif()
    if(n_frames GREATER 1)
        message(SEND_ERROR "${SRC}: ${n_frames} instantiation frames; diagnostics must not cascade")
        set(failed TRUE)
    endif()
    if(n_frames EQUAL 1)
        string(REGEX MATCH "In instantiation of [^\n]*vcodec::json::[a-z_]+" entry "${err}")
        if(NOT entry)
            message(SEND_ERROR "${SRC}: the instantiation frame does not name a vcodec::json entry point")
            set(failed TRUE)
        endif()
    endif()
    if(n_required GREATER 1)
        message(SEND_ERROR "${SRC}: ${n_required} 'required from' lines; diagnostics must not cascade")
        set(failed TRUE)
    endif()
endif()

if(failed)
    message(FATAL_ERROR "${SRC}: compile-fail expectations not met. Compiler output:\n${err}")
endif()
message(STATUS "${SRC}: rejected with the expected diagnostic")
