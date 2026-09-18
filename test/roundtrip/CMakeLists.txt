vcodec_add_test(test_roundtrip SOURCES roundtrip.cpp)
if(VCODEC_BUILD_CBOR)
    vcodec_add_test(test_roundtrip_cbor SOURCES cbor_roundtrip.cpp)
endif()
