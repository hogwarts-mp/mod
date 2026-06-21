// Type definitions for HogwartsMP CLIENT scripts (a resource's `mafiahub.client` entry).
//
// Client scripts run in-game in the sandboxed standalone V8 engine (no Node, no fs/net). These
// describe the builtins the client registers; they add nothing at runtime. Usage: see types/README.md.
//
// Hand-maintained: keep in sync with code/client/src/core/builtins/. Include EITHER this file or
// hogwartsmp-server.d.ts per resource, never both (they both declare `Core`).
//
// `console`, `setTimeout`, `setInterval`, `queueMicrotask` come from TypeScript's default lib (not
// redeclared here). The client engine provides those plus a CommonJS require for local .js files
// (no Node builtin modules), declared below since it isn't in the default lib.
declare function require(path: string): any;

// --- Game-facing builtins ---

declare const Game: {
    /** Show a line in the local chat UI. */
    notify(text: string): void;
    /**
     * Send a named event up to the SERVER's scripts (received via Core.Events.on(name,
     * (player, payload) => ...)). `payloadJson` is JSON.parsed on the server, so pass JSON text
     * (e.g. JSON.stringify(obj)).
     */
    emitServer(eventName: string, payloadJson: string): void;
};

/** The local player's world position (cm). */
interface Vec3 {
    x: number;
    y: number;
    z: number;
}

/** The local player's rotation in degrees. */
interface Rotator {
    pitch: number;
    yaw: number;
    roll: number;
}

declare const LocalPlayer: {
    /** The local pawn's world location, or null before it has spawned (e.g. still loading in). */
    getPosition(): Vec3 | null;
    /** The local pawn's rotation (degrees), or null before it has spawned. */
    getRotation(): Rotator | null;
};

// --- Event bus ---

interface ClientEvents {
    /**
     * A named event from the server (sent via the server's player.emit / World.emitAllClients).
     * `payload` is the JSON-parsed body (undefined when the server sent none).
     */
    on(event: string, handler: (payload?: any) => void): void;
}

declare const Core: {
    Events: ClientEvents;
};
