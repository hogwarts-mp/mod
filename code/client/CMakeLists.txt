set(HOGWARTSMP_CLIENT_FILES
    src/main.cpp
)

add_library(HogwartsMPClient SHARED ${HOGWARTSMP_CLIENT_FILES})
target_include_directories(HogwartsMPClient PRIVATE .)
target_link_libraries(HogwartsMPClient Framework FrameworkClient shlwapi)
target_link_options(HogwartsMPClient PRIVATE /SAFESEH:NO /MANIFEST:NO)

add_custom_command(TARGET HogwartsMPClient POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../files" "${CMAKE_BINARY_DIR}/bin/files/"
            )
