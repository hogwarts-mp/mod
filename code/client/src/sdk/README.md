# sdk/ — the game-interface layer

Everything about modeling and talking to Hogwarts Legacy's Unreal Engine
types lives here. The folder is split into four layers:

```
sdk/
├── ue/           Vendored Epic UE source — the foundation. DO NOT EDIT.
├── natives/      Sigscan glue: native function pointers + linker stubs.
├── reflection/   PRIMARY accessor: drive the game via UE's own reflection.
└── offsets/      LEGACY accessor: hand-RE'd SDK:: structs with fixed offsets.
```

## ue/ — vendored foundation

A stripped copy of Epic's `Runtime/{Core,CoreUObject,TraceLog}` headers. It is
a **third-party dependency, not our code** — never hand-edit it. Consumers
reach it through the `target_include_directories` entries in
`code/client/CMakeLists.txt`, so includes look like `#include "UObject/Class.h"`
(not `sdk/ue/...`). `natives/ue4_impl.cpp` supplies the few symbols these
headers expect to link against (`FMemory`, `FName::ToString`, ...).

## natives/ — sigscan glue

`ue4_natives.h` / `ue4_impl.cpp`: native function pointers resolved at init
from the central pattern table (`core/game_layout.h`) plus the linker glue that
makes `ue/` compile. Low-level; built on `ue/`.

## reflection/ — primary accessor (use this)

`ue4_reflection.h` / `.cpp`: read/write properties and call `UFunction`s by
**name**, resolved through the engine's own `FProperty`/`UFunction` chains
(`CallUFunction`, `FindPropertyInChain`, `ReadObjectProperty`, ...). No
per-class offsets to maintain, so it survives game patches. This is how new
features should talk to the game. Built on `natives/` + `ue/`.

## offsets/ — legacy accessor (don't grow this)

The original hand-reverse-engineered `SDK::` struct mirrors (`entities/`,
`components/`, `containers/`, `game/`, `types/`, ...), each modeling a game
class by hardcoded `pad[0x..]` field offsets. Still actively *used* in a few
places (and a handful compile: `containers/fname.cpp`, `types/uobject.cpp`,
`game/seasonchanger.cpp`, ...), but the offsets go stale on every game build.
**Prefer `reflection/` for anything new**; reach for `offsets/` only when
extending code that already depends on it.

## Include convention

Cross-directory includes use the **src-relative** form resolved via the `src`
include dir — `#include "sdk/reflection/ue4_reflection.h"`,
`#include "sdk/offsets/entities/a_biped_player.h"`. The `ue/` headers are the
sole exception (reached bare, via their own include dirs).
