# Structural rule 1 (spec §3, §13.6): nothing under include/vcodec/core/ may include a
# format header. Structural rule 2: nothing under include/ may include spike/.
# Usage: vcodec_layering_check(<path to include/vcodec>)  -- fails configure on a hit.
# Also runnable as a script:  cmake -DVCODEC_INCLUDE_DIR=<path> -P LayeringCheck.cmake
function(vcodec_layering_check root)
    file(GLOB_RECURSE core_headers "${root}/core/*.hpp")
    foreach(header IN LISTS core_headers)
        file(STRINGS "${header}" hits REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"].*(json|cbor|spike)/")
        if(hits)
            message(FATAL_ERROR "layering violation: ${header} includes a format header:\n  ${hits}")
        endif()
    endforeach()
    file(GLOB_RECURSE all_headers "${root}/*.hpp")
    foreach(header IN LISTS all_headers)
        file(STRINGS "${header}" hits REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"].*spike/")
        if(hits)
            message(FATAL_ERROR "layering violation: ${header} includes the spike:\n  ${hits}")
        endif()
        # Structural rule 3: only core/compiler.hpp may contain compiler conditionals.
        if(NOT header MATCHES "core/compiler\\.hpp$")
            file(STRINGS "${header}" hits REGEX "^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)[ \t].*(__GNUC__|__clang__|_MSC_VER|__GNUG__)")
            if(hits)
                message(FATAL_ERROR "layering violation: ${header} contains a compiler conditional (only core/compiler.hpp may):\n  ${hits}")
            endif()
        endif()
    endforeach()
    message(STATUS "vcodec: layering check passed (${root})")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND VCODEC_INCLUDE_DIR)
    vcodec_layering_check("${VCODEC_INCLUDE_DIR}")
endif()
