// Typed JS -> native bridge for the in-game creator.
//
// The framework's CEF renderer injects a global `callEvent(name, payloadStringOrNull)`
// that round-trips to the C++ Creator view's AddEventListener handlers (the cc:* events).
// This is a DIFFERENT transport from the launcher (which uses window.cefQuery) — the
// in-game web manager uses callEvent.
//
// When callEvent is absent (plain browser / design preview / `npm run dev`), calls fall
// back to console logging so the UI stays fully clickable without the game.

declare global {
  interface Window {
    callEvent?: (name: string, payload: string | null) => void;
    hmpCreator?: { open: () => void; close: () => void };
  }
}

export const isHost = typeof window.callEvent === "function";

function emit(name: string, payload?: unknown): void {
  const body =
    payload === undefined || payload === null
      ? null
      : typeof payload === "string"
        ? payload
        : JSON.stringify(payload);
  if (typeof window.callEvent === "function") {
    window.callEvent(name, body);
  } else {
    console.info("[creator-bridge] (no host) ->", name, body);
  }
}

export const bridge = {
  /** Tell the C++ view the page mounted (it shows/hides via window.hmpCreator). */
  ready(): void {
    emit("creator:ready", null);
  },
  /** Close the creator (host releases focus + control lock). */
  close(): void {
    emit("creator:close", null);
  },

  // ── Live CCC edits ────────────────────────────────────────────────────────
  /** Select option `index` (1-based) within a category — host maps it to the CCC apply. */
  setOption(category: string, index: number): void {
    emit("cc:option", { category, index });
  },
  /** Rebuild the character after a batch of edits. */
  reload(): void {
    emit("cc:reload", null);
  },
  /** Frame the live avatar for the current section (front view). */
  setCamera(cam: { dist: number; height: number; pitch: number; fov: number; shift: number }): void {
    emit("cc:camera", cam);
  },
  /** Rotate the avatar by a yaw delta (drag-to-inspect). */
  rotate(deltaYaw: number): void {
    emit("cc:rotate", { yaw: deltaYaw });
  },
  /** Freeze/resume the idle animation so the character holds still. */
  setFreeze(on: boolean): void {
    emit("cc:freeze", { on });
  },

  // ── Finalise ──────────────────────────────────────────────────────────────
  setVoice(tone: number): void {
    emit("cc:voice", { tone });
  },
  setPitch(value: number): void {
    emit("cc:pitch", { value });
  },
  /** Play a sample line in the player's voice at the current pitch. */
  previewVoice(): void {
    emit("cc:voicepreview", null);
  },
  setName(first: string, last: string): void {
    emit("cc:name", { first, last });
  },
  setDormitory(dorm: "witch" | "wizard"): void {
    emit("cc:dormitory", { dorm });
  },
  confirm(): void {
    emit("cc:confirm", null);
  },
};
