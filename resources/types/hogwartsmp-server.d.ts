// Type definitions for HogwartsMP SERVER scripts (a resource's `mafiahub.server` entry).
//
// These describe the builtins the server registers; they add nothing at runtime (the server runs
// plain JS) — they only give your editor autocomplete + type checking. Usage: see types/README.md.
//
// Hand-maintained: keep in sync with code/server/src/core/builtins/. Include EITHER this file or
// hogwartsmp-client.d.ts per resource, never both (they both declare `Core`).

// --- Runtime globals the engine provides. Declared here because the tsconfig uses lib "ES2020"
// (NOT "DOM") — the DOM lib's Web APIs (Storage, Location, ...) collide with our builtin names. ---
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

// --- Math / entity handles ---

/**
 * A 3D vector handle on an entity's transform. Mutate via set() then re-assign — writing .x/.y/.z
 * on the returned handle alone does NOT move the entity (it's a detached copy).
 */
interface Vector3 {
    x: number;
    y: number;
    z: number;
    set(x: number, y: number, z: number): void;
}

/** Base networked entity. */
interface Entity {
    /** Network id (stable for the entity's lifetime). */
    readonly id: number;
    /** World position. Read, call set(...), then assign back (see Vector3). */
    position: Vector3;
    /** Euler rotation in degrees. */
    rotation: Vector3;
    toString(): string;
}

/** A connected player (or server-owned NPC from World.spawnHuman). */
interface Human extends Entity {
    readonly nickname: string;
    /** Send a chat line to this player. */
    sendChat(message: string): void;
    /** Disconnect this player (real players only; no-op on NPCs). */
    kick(reason?: string): void;
    /**
     * Send a named event to THIS player's client scripts (received via Core.Events.on(name, payload)).
     * `payloadJson` is sent verbatim and JSON.parsed on the client, so pass JSON text
     * (e.g. JSON.stringify(obj)). Empty -> the client handler is called with no argument.
     */
    emit(eventName: string, payloadJson: string): void;
    /** Despawn. Affects only server-owned NPCs; real players are managed by the network layer. */
    destroy(): void;
}

// --- Global modules ---

declare const World: {
    /** Send a chat line to every connected player. */
    broadcastMessage(message: string): void;
    /** Send a chat line to one player. */
    sendChatMessage(human: Human, message: string): void;
    /**
     * Broadcast a named event to EVERY client's scripts (Core.Events). `payloadJson` is JSON.parsed
     * on the client, so pass JSON text. Empty -> handler called with no argument.
     */
    emitAllClients(eventName: string, payloadJson: string): void;
    /** Every connected player. Server-owned NPCs are excluded. */
    getPlayers(): Human[];
    /** The connected player with the given network id, or undefined. */
    getPlayer(id: number): Human | undefined;
    /** Number of connected players (cheaper than getPlayers().length). */
    getPlayerCount(): number;
    /** Spawn a server-owned NPC at a world position; despawn with the returned handle's destroy(). */
    spawnHuman(x: number, y: number, z: number): Human;
};

declare const Environment: {
    setWeather(name: string): void;
    setTime(hour: number, minute: number): void;
    setDate(day: number, month: number): void;
    /** 0 = spring, 1 = summer, 2 = autumn, 3 = winter. */
    setSeason(season: number): void;
};

/**
 * Persistent key/value store (backed by storage.json, flushed on every write). Values are strings —
 * wrap structured data with JSON.stringify / JSON.parse. Single global namespace (no per-player yet).
 */
declare const Storage: {
    get(key: string): string | undefined;
    set(key: string, value: string): void;
    has(key: string): boolean;
    /** Returns true if a key was removed. */
    delete(key: string): boolean;
    keys(): string[];
};

// --- Event bus ---

interface ServerEvents {
    on(event: "playerConnect", handler: (player: Human) => void): void;
    on(event: "playerDisconnect", handler: (player: Human) => void): void;
    on(event: "playerDied", handler: (player: Human) => void): void;
    on(event: "chatMessage", handler: (player: Human, message: string) => void): void;
    on(
        event: "chatCommand",
        handler: (player: Human, message: string, command: string, args: string[]) => void,
    ): void;
    /**
     * A custom event sent up from a client (via the client's Game.emitServer). `payload` is the
     * JSON-parsed body (undefined when the client sent none). NOTE: event name + payload are
     * attacker-controlled — validate them, and don't reuse reserved names for client-driven logic.
     */
    on(event: string, handler: (player: Human, payload?: any) => void): void;
}

declare const Core: {
    Events: ServerEvents;
};
