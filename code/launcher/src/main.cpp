#include <Windows.h>

#include <launcher/project.h>

int main (void) {
    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName = L"HogwartsMPClient.dll";
    config.executableName     = L"HogwartsLegacy.exe";
    config.name               = "HogwartsMP";
    config.launchType            = Framework::Launcher::ProjectLaunchType::DLL_INJECTION;
    config.platform           = Framework::Launcher::ProjectPlatform::STEAM;
    config.steamAppId         = 990080;
    config.alternativeWorkDir    = L"Phoenix/Binaries/Win64";
    config.additionalDLLInjectionArguments = L" -SaveToUserDir -UserDir=\"Hogwarts Legacy\"";
    config.useAlternativeWorkDir = true;
    config.additionalSearchPaths = {
        L"Engine\\Binaries\\ThirdParty\\DbgHelp",
        L"Engine\\Binaries\\ThirdParty\\NVIDIA\\GeForceNOW\\Win64",
        L"Engine\\Binaries\\ThirdParty\\NVIDIA\\NVaftermath\\Win64\\GFSDK_Aftermath_Lib",
        L"Engine\\Binaries\\ThirdParty\\Ogg\\Win64\\VS2015",
        L"Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv154\\Win64",
        L"Engine\\Binaries\\ThirdParty\\Windows\\XAudio2_9\\x64",
        L"Engine\\Plugins\\Runtime\\Intel\\XeSS\\Binaries\\ThirdParty\\Win64",
        L"Engine\\Plugins\\Runtime\\Nvidia\\Ansel\\Binaries\\ThirdParty",
        L"Engine\\Plugins\\Runtime\\Nvidia\\DLSS\\Binaries\\ThirdParty\\Win64",
        L"Engine\\Plugins\\Runtime\\Nvidia\\NVIDIAGfeSDK\\ThirdParty\\NVIDIAGfeSDK\\redist\\Win64",
        L"Engine\\Plugins\\Runtime\\Nvidia\\Streamline\\Binaries\\ThirdParty\\Win64",
        L"Engine\\Plugins\\Runtime\\Nvidia\\Streamline\\Binaries\\ThirdParty\\Win64\\sl",
        L"Phoenix\\Binaries\\Win64\\EOSSDK-Win64",
        L"Phoenix\\Binaries\\Win64",
        L"Phoenix\\Plugins\\ChromaSDKPlugin\\Binaries\\Win64",
        L"Phoenix\\Plugins\\Wwise\\ThirdParty\\x64_vc160\\Release\\bin",
    };

#ifdef FW_DEBUG
    config.allocateDeveloperConsole = false;
    config.developerConsoleTitle    = L"hogwartsmp: dev-console";
#endif

    Framework::Launcher::Project project(config);
    if (!project.Launch()) {
        return 1;
    }
    return 0;
}
