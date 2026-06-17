#include "core/server.h"

#include "shared/version.h"

int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions opts;
    opts.bindHost      = "0.0.0.0";
    opts.bindPort      = 27015;
    opts.webBindHost   = "0.0.0.0";
    opts.webBindPort   = 27016;
    opts.maxPlayers    = 512;
    opts.modName       = "HogwartsMP";
    opts.modSlug       = "hogwartsmp_server";
    opts.gameName      = "Hogwarts Legacy";
    opts.gameVersion   = HogwartsMP::Version::rel;
    opts.modVersion    = "0.1.0";
    opts.bindPassword  = "";
    opts.enableSignals = true;

    // Hogwarts is cm-scale (coords in the hundreds of thousands); size the interest grid to ~±20 km
    // with 100 m cells so culling is meaningful at the player's 500 m range (the ±10k default clamps).
    opts.worldConfig.streamWorldMin = -2000000.0f;
    opts.worldConfig.streamWorldMax = 2000000.0f;
    opts.worldConfig.streamCellSize = 10000.0f;

    opts.argc = argc;
    opts.argv = argv;

    HogwartsMP::Server server;
    if (!server.Init(opts)) {
        return 1;
    }
    server.Run();
    server.Shutdown();
    return 0;
}
