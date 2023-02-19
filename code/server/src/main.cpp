#include "core/server.h"

#include "shared/version.h"

int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions opts;
    opts.bindHost      = "0.0.0.0";
    opts.bindPort      = 27015;
    opts.webBindHost   = "0.0.0.0";
    opts.webBindPort   = 27016;
    opts.bindName      = "Hogwarts Legacy: Multiplayer Local Dev Server";
    opts.maxPlayers    = 512;
    opts.modName       = "HogwartsMP";
    opts.modSlug       = "hogwartsmp_server";
    opts.gameName      = "Hogwarts Legacy";
    opts.gameVersion   = HogwartsMP::Version::rel;
    opts.modVersion    = "0.1.0";
    opts.bindPassword  = "";
    opts.enableSignals = true;

    opts.argc = argc;
    opts.argv = argv;

    HogwartsMP::Server server;
    if (server.Init(opts) != Framework::Integrations::Server::ServerError::SERVER_NONE) {
        return 1;
    }
    server.Run();
    server.Shutdown();
    return 0;
}
