set(HOGWARTSMP_LAUNCHER_FILES
    src/main.cpp
)

add_executable(HogwartsMPLauncher ${HOGWARTSMP_LAUNCHER_FILES})
target_include_directories(HogwartsMPLauncher PRIVATE .)
target_link_libraries(HogwartsMPLauncher PRIVATE Framework FrameworkLoader)
