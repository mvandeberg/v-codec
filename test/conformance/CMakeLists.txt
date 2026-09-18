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

if(VCODEC_BUILD_CBOR)
    # v0.2 §13.3: RFC 8949 Appendix A vectors (cbor/test-vectors) plus the committed hostile
    # corpus and the JSON→CBOR transcoding check over JSONTestSuite.
    FetchContent_Declare(cbor_test_vectors
        GIT_REPOSITORY https://github.com/cbor/test-vectors.git
        GIT_TAG        aba89b653e484bc8573c22f3ff35641d79dfd8c1
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  does-not-exist)
    FetchContent_MakeAvailable(cbor_test_vectors)

    vcodec_add_test(test_conformance_cbor SOURCES cbor_appendix_a.cpp cbor_hostile.cpp cbor_transcode.cpp)
    target_compile_definitions(test_conformance_cbor PRIVATE
        VCODEC_CBOR_VECTORS="${cbor_test_vectors_SOURCE_DIR}/appendix_a.json"
        VCODEC_CBOR_HOSTILE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/cbor_hostile"
        VCODEC_JSONTESTSUITE_DIR="${jsontestsuite_SOURCE_DIR}/test_parsing")
endif()
