set(UE4_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src/sdk)

message(UE4_SOURCE_DIR="${UE4_SOURCE_DIR}")

set(HOGWARTSMP_CLIENT_FILES
    src/main.cpp
    src/core/hooks/engine.cpp
    src/core/hooks/gamemode.cpp
    src/core/hooks/player_controller.cpp
    src/core/hooks/render_device.cpp
    src/core/hooks/world.cpp
    src/core/modules/human.cpp
    src/core/application.cpp
    src/core/dev_features.cpp
    src/core/ue4_impl.cpp
    src/core/playground.cpp

    src/core/ui/chat.cpp
    src/core/ui/console.cpp

    src/game/game_input.cpp
    ${CMAKE_BINARY_DIR}/hogwartsmp_version.cpp
)

add_library(HogwartsMPClient SHARED ${HOGWARTSMP_CLIENT_FILES} "src/core/ue4_impl.cpp")
target_include_directories(HogwartsMPClient PRIVATE src
    ${UE4_SOURCE_DIR}/Runtime/Core/Public
    ${UE4_SOURCE_DIR}/Runtime/CoreUObject/Public
    ${UE4_SOURCE_DIR}/Runtime/TraceLog/Public
)

target_link_libraries(HogwartsMPClient Framework FrameworkClient shlwapi)
target_link_options(HogwartsMPClient PRIVATE /SAFESEH:NO /MANIFEST:NO)

target_compile_definitions(HogwartsMPClient PRIVATE 
    UBT_COMPILED_PLATFORM=Windows 
    UE_BUILD_SHIPPING=1 
    WITH_EDITOR=0 
    WITH_EDITORONLY_DATA=0
    WITH_ENGINE=1 
    WITH_UNREAL_DEVELOPER_TOOLS=0
    WITH_PLUGIN_SUPPORT=0
    WITH_SERVER_CODE=0
    IS_MONOLITHIC=1
    IS_PROGRAM=1
    PLATFORM_WINDOWS
    PLATFORM_64BITS
    CORE_API=
    COREUOBJECT_API=
    UNICODE
    _UNICODE
    NOMINMAX
    WINVER=0x0A00
)

add_custom_command(TARGET HogwartsMPClient POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../files" "${CMAKE_BINARY_DIR}/bin/files/"
            )
