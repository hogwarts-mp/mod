# HogwartsMP pre-launch launcher (windowed CEF host).
#
# FiveM-style launcher the user runs: shows the React UI and on connect writes connect.json
# + spawns the injector (HogwartsMPLauncher.exe). Reuses the vendored CEF; acts as its own
# CEF subprocess.
#
# Thin shell: the UI is NOT bundled. scheme_handler fetches it from the remote (GitHub Pages)
# into a local cache over WinHTTP, so there's no web/dist staging step here — build/publish
# the web app separately (web/ -> Pages). Override the source with HOGWARTSMP_LAUNCHER_REMOTE
# (mirror a local server) or HOGWARTSMP_LAUNCHER_URL (navigate straight at the Vite dev server).

set(HOGWARTSMP_LAUNCHER_UI_FILES
    src/main.cpp
    src/launcher_app.cpp
    src/launcher_client.cpp
    src/scheme_handler.cpp
    src/bridge_handler.cpp
    src/game_launch.cpp
)

# WIN32 => GUI subsystem (wWinMain, no console window).
add_executable(HogwartsMP WIN32 ${HOGWARTSMP_LAUNCHER_UI_FILES})
target_include_directories(HogwartsMP PRIVATE .)
target_link_libraries(HogwartsMP PRIVATE CEF nlohmann_json winhttp)
target_compile_definitions(HogwartsMP PRIVATE _USE_MATH_DEFINES NOMINMAX UNICODE _UNICODE)
set_target_properties(HogwartsMP PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# CEF runtime DLLs + resources land in bin/ via this target.
if(TARGET CEFCopyDlls)
    add_dependencies(HogwartsMP CEFCopyDlls)
endif()

# Ship static assets (self-hosted fonts) next to the exe, served under hmp://app/local/.
# Mirrors MafiaMP's files/ convention; the project root is two dirs up from here.
add_custom_command(TARGET HogwartsMP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../files" "${CMAKE_BINARY_DIR}/bin/files"
    COMMENT "Staging launcher assets (files -> bin/files)"
    VERBATIM
)
