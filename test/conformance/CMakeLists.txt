# §13.3: the nst/JSONTestSuite corpus, fetched at configure time, never vendored.
include(FetchContent)
FetchContent_Declare(jsontestsuite
    GIT_REPOSITORY https://github.com/nst/JSONTestSuite.git
    GIT_TAG        1ef36fa01286573e846ac449e8683f8833c5b26a
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  does-not-exist)   # no CMakeLists in the corpus; populate only
FetchContent_MakeAvailable(jsontestsuite)

vcodec_add_test(test_conformance SOURCES jsontestsuite.cpp)
target_compile_definitions(test_conformance PRIVATE
    VCODEC_JSONTESTSUITE_DIR="${jsontestsuite_SOURCE_DIR}/test_parsing"
    VCODEC_I_DISPOSITIONS="${CMAKE_CURRENT_SOURCE_DIR}/i_dispositions.txt"
    VCODEC_I_DISPOSITIONS_OUT="${CMAKE_CURRENT_BINARY_DIR}/i_dispositions.observed.txt")
