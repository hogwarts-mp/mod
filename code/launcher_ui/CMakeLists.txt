# HogwartsMPLauncher — the single FiveM-style launcher the user runs.
#
# One exe: windowed CEF UI (server browser) + the injector. On connect the UI writes
# connect.json and arms a launch; after CEF tears down, main() fetches the avatar pak and
# injects HogwartsMPClient.dll into Hogwarts Legacy (Framework::Launcher::Project). Reuses
# the vendored CEF; acts as its own CEF subprocess.
#
# Thin shell: the UI is NOT bundled. scheme_handler fetches it from the remote (GitHub Pages)
# into a local cache over WinHTTP, so there's no web/dist staging step here — build/publish
# the web app separately (web/ -> Pages). Override the source with HOGWARTSMP_LAUNCHER_REMOTE
# (mirror a local server) or HOGWARTSMP_LAUNCHER_URL (navigate straight at the Vite dev server).

set(HOGWARTSMP_LAUNCHER_FILES
    src/main.cpp
    src/launcher_app.cpp
    src/launcher_client.cpp
    src/scheme_handler.cpp
    src/bridge_handler.cpp
    src/game_launch.cpp
)

# Entry is main(); FrameworkLoader's PUBLIC link options force /SUBSYSTEM:windows +
# /ENTRY:mainCRTStartup, so there's no console window despite the console-style entry.
add_executable(HogwartsMPLauncher ${HOGWARTSMP_LAUNCHER_FILES})
target_include_directories(HogwartsMPLauncher PRIVATE .)
# CEF/nlohmann/winhttp for the UI + cache; Framework/FrameworkLoader for pak fetch + injection.
target_link_libraries(HogwartsMPLauncher PRIVATE CEF nlohmann_json winhttp Framework FrameworkLoader)
target_compile_definitions(HogwartsMPLauncher PRIVATE _USE_MATH_DEFINES NOMINMAX UNICODE _UNICODE)
set_target_properties(HogwartsMPLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# CEF runtime DLLs + resources land in bin/ via this target.
if(TARGET CEFCopyDlls)
    add_dependencies(HogwartsMPLauncher CEFCopyDlls)
endif()

# Ship static assets (self-hosted fonts) next to the exe, served under hmp://app/local/.
# Mirrors MafiaMP's files/ convention; the project root is two dirs up from here.
add_custom_command(TARGET HogwartsMPLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../files" "${CMAKE_BINARY_DIR}/bin/files"
    COMMENT "Staging launcher assets (files -> bin/files)"
    VERBATIM
)
