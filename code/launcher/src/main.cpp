#include <Windows.h>

#include <launcher/project.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName = L"HogwartsMPClient.dll";
    config.executableName     = L"HogwartsLegacy.exe";
    config.name               = "HogwartsMP";
    config.platform           = Framework::Launcher::ProjectPlatform::STEAM;
    config.steamAppId         = 990080;
    config.alternativeWorkDir    = L"Phoenix/Binaries/Win64";
    config.useAlternativeWorkDir = true;

#ifdef FW_DEBUG
    config.allocateDeveloperConsole = true;
    config.developerConsoleTitle    = L"hogwartsmp: dev-console";
#endif

    Framework::Launcher::Project project(config);
    if (!project.Launch()) {
        return 1;
    }
    return 0;
}
