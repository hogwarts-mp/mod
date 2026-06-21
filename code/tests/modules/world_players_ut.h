#pragma once

#include "scripting/node_engine.h"

#include "core/builtins/builtins.h"
#include "core/modules/human.h"

#include "shared/game/human.h"

#include <core_modules.h>
#include <networking/network_server.h>
#include <networking/replication/entity_registry.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/types.h>

#include <string>

// Integration test for the World player-query builtins. Stands up a real NetworkServer (binds a
// loopback UDP socket) populated with human entities, then queries them from JavaScript exactly as a
// gamemode would. A live peer is mandatory: ReplicaManager3::Reference() — reached through
// CreateEntity — dereferences the attached RakNet peer, so entities cannot enter the replica list
// (which the queries enumerate) without one, and the NetworkID manager that getPlayer() resolves
// through is only wired up in ReplicationManager::Init().
MODULE(world_players, {
    using namespace Framework::Scripting;
    using Framework::Networking::NetworkServer;
    using Framework::Networking::Replication::EntityRegistry;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::ReplicationManager;
    using HogwartsMP::Shared::HumanEntity;

    IT("World queries reflect the server's connected players and exclude NPCs", {
        HogwartsMP::Core::Modules::Human::Register();

        // Bind on an uncommon loopback port to avoid clashing with a running dev server.
        NetworkServer server;
        const auto initResult = server.Init("127.0.0.1", 28960, 16);
        EQUALS(static_cast<bool>(initResult), true);

        auto *repl = server.GetReplicationManager();
        NEQUALS(repl, (ReplicationManager *)nullptr);
        Framework::CoreModules::SetNetworkPeer(&server);
        Framework::CoreModules::SetReplication(repl);

        const auto typeId    = EntityRegistry::Get().TypeId(HogwartsMP::Shared::kHumanTypeName);
        const auto makeHuman = [&](MafiaNet::PeerGuid owner, const char *nick) -> HumanEntity * {
            auto *h = static_cast<HumanEntity *>(repl->CreateEntity(typeId));
            if (h) {
                // Set ownership directly rather than via SetOwner, which would RPC a non-connected peer.
                h->ownerGUID = owner;
                h->nickname  = nick;
            }
            return h;
        };

        // Two connected players (distinct owners) and one server-owned NPC (left unowned).
        auto *p1  = makeHuman(static_cast<MafiaNet::PeerGuid>(1), "Alice");
        auto *p2  = makeHuman(static_cast<MafiaNet::PeerGuid>(2), "Bob");
        auto *npc = makeHuman(MafiaNet::UNASSIGNED_PEER_GUID, "NPC");
        NEQUALS(p1, (HumanEntity *)nullptr);
        NEQUALS(p2, (HumanEntity *)nullptr);
        NEQUALS(npc, (HumanEntity *)nullptr);

        // Resolution must work before the JS layer leans on it (getPlayers builds Human handles, which
        // resolve by NetworkID); assert it here so a resolution gap fails cleanly instead of throwing.
        EQUALS(repl->GetEntity<HumanEntity>(p1->GetNetworkID()), p1);

        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::Local<v8::Object> global       = context->Global();
            v8::Local<v8::Object> frameworkObj = v8::Object::New(isolate);
            global->Set(context, v8pp::to_v8(isolate, "Framework"), frameworkObj).Check();
            HogwartsMP::Scripting::Builtins::Register(isolate, global, frameworkObj);

            const auto evalBool = [&](const std::string &src) -> bool {
                v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, src.c_str()).ToLocalChecked();
                v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
                v8::Local<v8::Value> result  = script->Run(context).ToLocalChecked();
                return result->BooleanValue(isolate);
            };
            const auto evalInt = [&](const std::string &src) -> int32_t {
                v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, src.c_str()).ToLocalChecked();
                v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
                v8::Local<v8::Value> result  = script->Run(context).ToLocalChecked();
                return result->Int32Value(context).FromMaybe(-1);
            };

            const auto p1Id  = std::to_string(static_cast<uint64_t>(p1->GetNetworkID()));
            const auto npcId = std::to_string(static_cast<uint64_t>(npc->GetNetworkID()));

            // The two owned players count and list; the unowned NPC is excluded from both.
            EQUALS(evalInt("World.getPlayerCount()"), 2);
            EQUALS(evalInt("World.getPlayers().length"), 2);
            EQUALS(evalBool("World.getPlayers().every(p => typeof p.id === 'number' && typeof p.nickname === 'string')"), true);

            // getPlayer resolves a connected player by id (with its data) and rejects NPCs / unknown ids.
            EQUALS(evalBool("World.getPlayer(" + p1Id + ") !== undefined"), true);
            EQUALS(evalBool("World.getPlayer(" + p1Id + ").nickname === 'Alice'"), true);
            EQUALS(evalBool("World.getPlayer(" + npcId + ") === undefined"), true);
            EQUALS(evalBool("World.getPlayer(999999) === undefined"), true);
        }
        engine.Shutdown();

        // Destroy entities while the peer is still up (BroadcastDestruction touches it), then clear the
        // module registry and close the socket so later modules start clean.
        repl->DestroyEntity(p1);
        repl->DestroyEntity(p2);
        repl->DestroyEntity(npc);
        Framework::CoreModules::Reset();
        server.Shutdown();
    });
});
