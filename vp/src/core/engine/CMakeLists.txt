file(GLOB_RECURSE HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(engine
		dma_ctrl.cpp
		${HEADERS})

target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
