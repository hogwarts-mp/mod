#include "application.h"

namespace HogwartsMP::Core {
    std::unique_ptr<Application> gApplication = nullptr;

    bool Application::PostInit() {
        return true;
    }

    bool Application::PreShutdown() {
        return true;
    }

    void Application::PostUpdate() {
        // Tick discord instance - Temporary
        const auto discordApi = Core::gApplication->GetPresence();
        if (discordApi && discordApi->IsInitialized()) {
            discordApi->SetPresence("Broomstick", "Flying around", discord::ActivityType::Playing);
        }
    }
}
