// Type definitions for HogwartsMP CLIENT scripts (a resource's `mafiahub.client` entry).
//
// Client scripts run in-game in the sandboxed standalone V8 engine (no Node, no fs/net). These
// describe the builtins the client registers; they add nothing at runtime. Usage: see types/README.md.
//
// Hand-maintained: keep in sync with code/client/src/core/builtins/. Include EITHER this file or
// hogwartsmp-server.d.ts per resource, never both (they both declare `Core`).

// --- Runtime globals the client V8 engine provides. Declared here because the tsconfig uses lib
// "ES2020" (NOT "DOM") — the DOM lib's Web APIs collide with builtin names. ---
declare var console: {
    log(...args: any[]): void;
    info(...args: any[]): void;
    warn(...args: any[]): void;
    error(...args: any[]): void;
    debug(...args: any[]): void;
};
declare function setTimeout(handler: (...args: any[]) => void, ms?: number, ...args: any[]): number;
declare function setInterval(handler: (...args: any[]) => void, ms?: number, ...args: any[]): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;
declare function queueMicrotask(callback: () => void): void;
/** CommonJS require for local .js files within the resource (no Node builtin modules). */
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
    /**
     * Generic reflection read of a property off the local pawn. `path` is a property name, or a dotted
     * path that hops object properties to reach a component/sub-object (e.g.
     * `"HealthComponent.CurrentHealth"`). Supports scalar types (bool/int/byte/enum/float/double) and
     * name/string. Returns `null` when there's no pawn, and `undefined` when the property isn't found
     * or is an unsupported type (structs/objects aren't read). Use `getPropNames(path)` to discover
     * what's readable at each level.
     */
    getProp(path: string): number | boolean | string | null | undefined;
    /**
     * Property names of the local pawn, or — given a dotted object path — of the object reached by
     * hopping it (e.g. `getPropNames("HealthComponent")`). A discovery aid for `getProp` (can be
     * large); empty if there's no pawn or the path doesn't resolve.
     */
    getPropNames(path?: string): string[];
    /**
     * Teleport the local player to a named fast-travel point (via the game's FastTravelManager).
     * Returns `true` if the fast-travel was **requested** — i.e. `name` is a known destination and the
     * manager is available — which is not a guarantee the game finished the teleport. Returns `false`
     * if `name` isn't a known destination (see `getFastTravelLocations`) or fast travel isn't available
     * right now (e.g. not in-world). Destinations are game-validated, so this can't place you out of bounds.
     */
    fastTravel(name: string): boolean;
    /** The known fast-travel destination names accepted by `fastTravel`. */
    getFastTravelLocations(): string[];
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
