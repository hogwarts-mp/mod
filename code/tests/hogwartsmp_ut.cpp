#define UNIT_MAX_MODULES 8
#include "logging/logger.h"
#include "unit.h"

#include "modules/chat_command_ut.h"
#include "modules/rpc_ut.h"
#include "modules/js_builtins_ut.h"
#include "modules/storage_ut.h"
#include "modules/world_players_ut.h"

int main() {
    UNIT_CREATE("HogwartsMPTests");

    Framework::Logging::GetInstance()->PauseLogging(true);

    UNIT_MODULE(chat_command);
    UNIT_MODULE(rpc);
    UNIT_MODULE(js_builtins);
    UNIT_MODULE(storage);
    UNIT_MODULE(world_players);

    return UNIT_RUN();
}
