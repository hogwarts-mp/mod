set(HOGWARTSMP_SERVER_FILES
    src/main.cpp
    src/core/server.cpp

    src/core/builtins/events.cpp
    src/core/builtins/human.cpp

    src/core/modules/human.cpp

    ${CMAKE_BINARY_DIR}/hogwartsmp_version.cpp
)

add_executable(HogwartsMPServer ${HOGWARTSMP_SERVER_FILES})
target_include_directories(HogwartsMPServer PRIVATE src)
# GNU ld is order-dependent: dependents before dependencies
target_link_libraries(HogwartsMPServer FrameworkServer MafiaHubServices Framework)

# Match FrameworkServer's compile definitions
if(WIN32)
    target_compile_definitions(HogwartsMPServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
