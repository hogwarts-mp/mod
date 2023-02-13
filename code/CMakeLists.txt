include_directories(${CMAKE_SOURCE_DIR}/code/framework ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CL_64)
    add_subdirectory(client)
    add_subdirectory(launcher)
endif()

add_subdirectory(server)