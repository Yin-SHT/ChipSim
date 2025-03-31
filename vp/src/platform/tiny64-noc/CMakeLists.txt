file(GLOB_RECURSE HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

file(GLOB_RECURSE NOXIM_SOURCES ${CMAKE_SOURCE_DIR}/src/noxim/src/*.cpp)

add_executable(tiny64-vp-noc
        tiny64-noc-main.cpp
        ${HEADERS}
        ${NOXIM_SOURCES})

# 添加 noxim 头文件路径
target_include_directories(tiny64-vp-noc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/noxim/src
    ${CMAKE_SOURCE_DIR}/src/noxim/src/routingAlgorithms
    ${CMAKE_SOURCE_DIR}/src/noxim/src/selectionStrategies
    ${CMAKE_SOURCE_DIR}/src/core/engine
)

target_link_libraries(tiny64-vp-noc rv64 engine platform-common gdb-mc ${Boost_LIBRARIES} ${SystemC_LIBRARIES} ${YamlCpp_LIBRARIES} pthread)

INSTALL(TARGETS tiny64-vp-noc RUNTIME DESTINATION bin)
