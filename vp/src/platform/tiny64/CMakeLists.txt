file(GLOB_RECURSE HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_executable(tiny64-vp
        tiny64_main.cpp
        ${HEADERS})

target_link_libraries(tiny64-vp rv64 engine platform-common gdb-mc ${Boost_LIBRARIES} ${SystemC_LIBRARIES} pthread)

INSTALL(TARGETS tiny64-vp RUNTIME DESTINATION bin)
