set(HOGWARTSMP_TESTS_FILES
    hogwartsmp_ut.cpp

    ../server/src/core/builtins/entity.cpp
    ../server/src/core/builtins/events.cpp
    ../server/src/core/builtins/human.cpp
    ../server/src/core/modules/human.cpp
)

add_executable(HogwartsMPTests ${HOGWARTSMP_TESTS_FILES})

target_include_directories(HogwartsMPTests PRIVATE
    .
    ../server/src
    ${CMAKE_SOURCE_DIR}/code/tests
)
# GNU ld is order-dependent: dependents before dependencies
target_link_libraries(HogwartsMPTests FrameworkServer MafiaHubServices Framework libnode)

# Tests use Node.js APIs through the scripting engine
target_compile_definitions(HogwartsMPTests PRIVATE NODE_WANT_INTERNALS=1)

if(WIN32)
    target_compile_definitions(HogwartsMPTests PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

add_custom_target(RunHogwartsMPTests
        COMMAND $<TARGET_FILE:HogwartsMPTests>
        DEPENDS $<TARGET_FILE:HogwartsMPTests>)
