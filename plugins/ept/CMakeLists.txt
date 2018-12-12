add_definitions(-DHAVE_JSONCPP=1)


PDAL_ADD_PLUGIN(reader_libname reader ept
    FILES
        io/EptReader.cpp
        io/private/arbiter.cpp
    LINK_WITH
        ${PDAL_JSONCPP_LIB_NAME}
)

target_compile_definitions(${reader_libname} PRIVATE ARBITER_CURL=1)
target_link_libraries(${reader_libname} PRIVATE ${CURL_LIBRARIES})

target_include_directories(${reader_libname} PRIVATE
    ${PDAL_JSONCPP_INCLUDE_DIR}
    ${PDAL_VENDOR_DIR}
    io/private
    ../../io)
