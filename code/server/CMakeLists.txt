set(HOGWARTSMP_SERVER_FILES
    src/main.cpp
    src/core/server.cpp

    src/core/modules/human.cpp

    ${CMAKE_BINARY_DIR}/hogwartsmp_version.cpp
)

add_executable(HogwartsMPServer ${HOGWARTSMP_SERVER_FILES})
target_include_directories(HogwartsMPServer PRIVATE src)
target_link_libraries(HogwartsMPServer Framework FrameworkServer)
