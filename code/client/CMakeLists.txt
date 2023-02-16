set(HOGWARTSMP_CLIENT_FILES
    src/main.cpp
    src/core/hooks/engine.cpp
    src/core/hooks/gamemode.cpp
    src/core/hooks/player_controller.cpp
    src/core/modules/human.cpp
    src/core/application.cpp
    ${CMAKE_BINARY_DIR}/hogwartsmp_version.cpp
)

add_library(HogwartsMPClient SHARED ${HOGWARTSMP_CLIENT_FILES})
target_include_directories(HogwartsMPClient PRIVATE src)
target_link_libraries(HogwartsMPClient Framework FrameworkClient shlwapi)
target_link_options(HogwartsMPClient PRIVATE /SAFESEH:NO /MANIFEST:NO)

add_custom_command(TARGET HogwartsMPClient POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../files" "${CMAKE_BINARY_DIR}/bin/files/"
            )
