# Scripting type definitions

Type definitions for the HogwartsMP scripting API. They are an **authoring aid only** — the server
(libnode) and client (sandboxed V8) both run plain JavaScript, so these `.d.ts` files add nothing at
runtime. They give your editor autocomplete, signature checking, and a machine-checked spec of the
builtins.

- [`hogwartsmp-server.d.ts`](hogwartsmp-server.d.ts) — for a resource's `mafiahub.server` script
  (`World`, `Environment`, `Storage`, `Human`, server `Core.Events`).
- [`hogwartsmp-client.d.ts`](hogwartsmp-client.d.ts) — for a resource's `mafiahub.client` script
  (`Game`, `LocalPlayer`, client `Core.Events`).

> Include **exactly one** of the two per file/resource (server *or* client) — both declare `Core`,
> so loading both in one TS project conflicts.

## Using them with plain JavaScript (no build step)

Add a check directive + reference at the top of your script. Nothing gets compiled; your editor just
type-checks the file:

```js
// @ts-check
/// <reference path="../../types/hogwartsmp-server.d.ts" />

const Events = Core.Events;
Events.on("playerConnect", (player) => {
    player.sendChat(`Welcome, ${player.nickname}!`); // autocompleted + checked
});
```

(Adjust the relative path to wherever your script sits, e.g. `../../types/hogwartsmp-client.d.ts`
from `gamemode/client/main.js`.)

Use `"lib": ["ES2020"]` (as the gamemode's tsconfigs do) — **not** `"DOM"`. The DOM library defines
Web APIs whose names collide with our builtins (e.g. `Storage`). `console`, `setTimeout`, etc. are
declared by the `.d.ts` files themselves, so they work under `ES2020` without `DOM` or `@types/node`.

## Using them with TypeScript (optional)

If you'd rather author in `.ts`, compile to `.js` and point the manifest at the emitted `.js` — the
runtime only loads JavaScript. A minimal per-resource `tsconfig.json`:

```json
{
  "compilerOptions": { "target": "ES2020", "strict": true, "outDir": "." },
  "include": ["**/*.ts", "../../types/hogwartsmp-server.d.ts"]
}
```

## Gotchas

- **Node APIs on the server.** The server is full Node, but these defs intentionally don't bundle
  Node typings (the tsconfigs use `"types": []` to avoid `@types/node` leaking globals in). So
  `require`, `process`, `Buffer`, etc. type-check as undefined even though they exist at runtime. If a
  server script uses them, add `@types/node` and set `"types": ["node"]` in that resource's tsconfig.
  (The *client* defs declare `require` directly because the client isn't Node — there's no
  `@types/node` for the sandboxed V8 engine.)
- **Multiple files in one resource share global scope.** With `"module": "CommonJS"`, a `.js` file
  with no `import`/`export` is a global script, so two files that both write `const Events =
  Core.Events;` at top level collide ("duplicate identifier"). Each resource ships a single `main.js`
  today, so this only bites once resources grow — give each file at least one `export {}` (making it a
  module) or use distinct top-level names.

## Keeping them current

These are **hand-maintained** — there is no generator from the C++ (v8pp) bindings. When a builtin
changes, update the matching `.d.ts`:
- server builtins: `code/server/src/core/builtins/` (`world.h`, `human.{h,cpp}`, `storage.h`)
- client builtins: `code/client/src/core/builtins/` (`game.{h,cpp}`)
