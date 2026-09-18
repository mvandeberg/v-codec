# Compile-time budget (spec §13.7, §14 item 12). Script mode:
#
#   cmake -DCXX=<compiler> "-DFLAGS=<flag;list>" -DSOURCE=<file.cpp> -DBASELINE=<baseline.txt>
#         [-DRUNS=3] [-DTOLERANCE=0.15] [-DRECORD=ON] -P CompileTimeBudget.cmake
#
# Compiles SOURCE with `-c` RUNS times using the same compiler and flags as the benchmark
# targets, takes the median wall-clock time and compares it with the number recorded in
# BASELINE. Fails when the median is more than TOLERANCE (fraction) slower. With RECORD=ON
# it instead writes the measured median (plus a machine description) into BASELINE.
#
# BASELINE format: lines starting with '#' are comments; the first other non-empty line is
# the baseline in seconds, e.g. "3.412". All arithmetic below is in integer microseconds
# because math(EXPR) has no floating point.
cmake_minimum_required(VERSION 3.28)

foreach(v CXX FLAGS SOURCE BASELINE)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "CompileTimeBudget.cmake: -D${v}=... is required")
    endif()
endforeach()
if(NOT DEFINED RUNS)
    set(RUNS 3)
endif()
if(NOT DEFINED TOLERANCE)
    set(TOLERANCE 0.15)
endif()

# "1.25" -> 1250000 (micro-units). Truncates beyond six decimals.
function(_to_micro str out)
    string(STRIP "${str}" s)
    if(NOT s MATCHES "^([0-9]+)(\\.([0-9]*))?$")
        message(FATAL_ERROR "CompileTimeBudget.cmake: '${str}' is not a non-negative decimal number")
    endif()
    set(whole "${CMAKE_MATCH_1}")
    set(frac "${CMAKE_MATCH_3}000000")
    string(SUBSTRING "${frac}" 0 6 frac)
    math(EXPR micro "${whole} * 1000000 + ${frac}")
    set(${out} ${micro} PARENT_SCOPE)
endfunction()

# 1250000 -> "1.250"
function(_fmt_seconds micro out)
    math(EXPR whole "${micro} / 1000000")
    math(EXPR milli "(${micro} % 1000000) / 1000")
    string(LENGTH "${milli}" n)
    while(n LESS 3)
        set(milli "0${milli}")
        string(LENGTH "${milli}" n)
    endwhile()
    set(${out} "${whole}.${milli}" PARENT_SCOPE)
endfunction()

function(_now_micro out)
    string(TIMESTAMP s "%s" UTC)
    string(TIMESTAMP us "%f" UTC)
    math(EXPR t "${s} * 1000000 + ${us}")
    set(${out} ${t} PARENT_SCOPE)
endfunction()

set(_obj "${CMAKE_CURRENT_BINARY_DIR}/compile_time_budget.o")
set(_samples "")
foreach(i RANGE 1 ${RUNS})
    _now_micro(t0)
    execute_process(
        COMMAND ${CXX} ${FLAGS} -c ${SOURCE} -o ${_obj}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    _now_micro(t1)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "compile_time_budget: compilation failed (exit ${rc}):\n${out}\n${err}")
    endif()
    math(EXPR dt "${t1} - ${t0}")
    _fmt_seconds(${dt} dt_s)
    message(STATUS "run ${i}/${RUNS}: ${dt_s} s")
    list(APPEND _samples ${dt})
endforeach()
file(REMOVE "${_obj}")

list(SORT _samples COMPARE NATURAL)
list(LENGTH _samples n)
math(EXPR mid "${n} / 2")
list(GET _samples ${mid} median)
_fmt_seconds(${median} median_s)

if(RECORD)
    cmake_host_system_information(RESULT cpu QUERY PROCESSOR_DESCRIPTION)
    cmake_host_system_information(RESULT cores QUERY NUMBER_OF_LOGICAL_CORES)
    cmake_host_system_information(RESULT os QUERY OS_NAME)
    cmake_host_system_information(RESULT osver QUERY OS_RELEASE)
    execute_process(COMMAND ${CXX} --version OUTPUT_VARIABLE cxxver ERROR_QUIET)
    string(REGEX REPLACE "\n.*" "" cxxver "${cxxver}")
    string(TIMESTAMP when "%Y-%m-%d" UTC)
    string(REPLACE ";" " " flags_str "${FLAGS}")
    file(WRITE "${BASELINE}"
        "# Compile-time baseline for bench/compile_time.cpp: median wall-clock seconds of ${RUNS} runs.\n"
        "# Recorded ${when} on ${cpu} (${cores} logical cores), ${os} ${osver}\n"
        "# Compiler: ${cxxver}\n"
        "# Flags: ${flags_str} -c\n"
        "# Re-record with: cmake -DRECORD=ON ... -P bench/CompileTimeBudget.cmake (see file header)\n"
        "${median_s}\n")
    message(STATUS "compile_time_budget: recorded baseline ${median_s} s to ${BASELINE}")
    return()
endif()

if(NOT EXISTS "${BASELINE}")
    message(FATAL_ERROR "compile_time_budget: no baseline at ${BASELINE}; run with -DRECORD=ON to create one")
endif()
file(STRINGS "${BASELINE}" lines)
set(baseline_str "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    set(baseline_str "${line}")
    break()
endforeach()
if(baseline_str STREQUAL "")
    message(FATAL_ERROR "compile_time_budget: ${BASELINE} contains no baseline number")
endif()
_to_micro("${baseline_str}" baseline)
_to_micro("${TOLERANCE}" tol)
math(EXPR limit "${baseline} + (${baseline} * ${tol}) / 1000000")
_fmt_seconds(${baseline} baseline_s)
_fmt_seconds(${limit} limit_s)
math(EXPR delta_pct "((${median} - ${baseline}) * 1000) / ${baseline}")   # tenths of a percent
math(EXPR delta_whole "${delta_pct} / 10")
math(EXPR delta_frac "${delta_pct} % 10")
if(delta_frac LESS 0)
    math(EXPR delta_frac "-${delta_frac}")
endif()

message(STATUS "compile_time_budget: median ${median_s} s, baseline ${baseline_s} s, limit ${limit_s} s (${delta_whole}.${delta_frac}% vs baseline)")
if(median GREATER limit)
    message(FATAL_ERROR "compile_time_budget: FAILED — ${median_s} s exceeds ${baseline_s} s by more than ${TOLERANCE} (limit ${limit_s} s)")
endif()
message(STATUS "compile_time_budget: OK")
