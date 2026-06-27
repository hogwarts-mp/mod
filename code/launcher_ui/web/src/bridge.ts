// Typed JS -> native bridge for the CEF host.
//
// The native launcher exposes a handler that receives JSON requests. We prefer CEF's
// message router (`window.cefQuery`); when it's absent (e.g. running the UI in a normal
// browser for design preview, or `npm run dev`), calls fall back to console logging so
// the UI stays fully clickable without a host.
//
// Keep this the single source of truth for the launcher<->client contract. The C++ side
// parses { action, ... } and acts: `connect` writes the connect-config + spawns the game
// injector; the window actions drive the native window.

export type NativeAction =
  | { action: "connect"; address: string }
  | { action: "window"; op: "minimize" | "maximize" | "close" };

interface CefQueryRequest {
  request: string;
  persistent?: boolean;
  onSuccess?: (response: string) => void;
  onFailure?: (errorCode: number, errorMessage: string) => void;
}

declare global {
  interface Window {
    cefQuery?: (req: CefQueryRequest) => number;
  }
}

// True when running inside the native CEF launcher (the message router injects
// window.cefQuery before page scripts run). Used to fill the OS window edge-to-edge
// vs. showing the centered design-preview card in a plain browser.
export const isHost = typeof window.cefQuery === "function";

function send(payload: NativeAction): void {
  const request = JSON.stringify(payload);
  if (typeof window.cefQuery === "function") {
    window.cefQuery({
      request,
      onSuccess: () => {},
      onFailure: (code, message) =>
        console.error(`[bridge] native call failed (${code}): ${message}`, payload),
    });
    return;
  }
  // No host present — design-preview / dev fallback.
  console.info("[bridge] (no native host) ->", payload);
}

export const bridge = {
  /** Hand the chosen server to the client and launch the game. */
  connect(address: string): void {
    const addr = address.trim();
    if (!addr) return;
    send({ action: "connect", address: addr });
  },
  minimize(): void {
    send({ action: "window", op: "minimize" });
  },
  maximize(): void {
    send({ action: "window", op: "maximize" });
  },
  close(): void {
    send({ action: "window", op: "close" });
  },
};
