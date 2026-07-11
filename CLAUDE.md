# EOSEmu

A clean-room re-implementation of Epic Online Services SDK **1.19.1.2**, built as a **drop-in binary replacement** for `EOSSDK-Win64-Shipping.dll`. Auth is shimmed to always succeed; the online backend is replaced with LAN-local transport, so two EOSEmu processes on the same network can discover each other and play.

**64-bit only.** EOS SDK 1.19 dropped 32-bit (Win32) support, so EOSEmu is x64-only on Windows — there is no `Win32-Shipping` variant to produce.

## Layout

This repository root **is** the project — everything we write lives here.

| Path | Status | Notes |
| --- | --- | --- |
| `src/` | ours | One subdir per concern: `core/`, `interfaces/`, `net/`, `overlay/`, plus committed `generated/` stubs. |
| `tests/` | ours | C harness apps + `run_*.ps1` drivers. |
| `tools/` | ours | `gen_stubs.py`, `check_exports.py`, `hand_implemented.txt`. |
| `third_party/imgui/` | vendored | Dear ImGui at a pinned commit. |
| `third_party/EOSSDK/` | **you provide** | Epic's EOS SDK archive, unpacked whole — **not committed** (see `third_party/EOSSDK/place_sdk_here.txt`). Contains `SDK/` (with `SDK/Include/*.h` the specification and `SDK/Bin/*.dll` the behavioural oracle), `Samples/` (Epic's sample apps), and `ThirdPartyNotices/`. |

**Epic's SDK is not bundled** (its license is Epic's). Download the EOS C SDK
**1.19.1.2** from the Epic Dev Portal and unzip the whole archive into
`third_party/EOSSDK/` so `third_party/EOSSDK/SDK/Include/eos_sdk.h` exists, or
point the build at an SDK elsewhere with
`-DEOSEMU_SDK_INCLUDE_DIR=<path to SDK/Include>`. Never edit anything under
`third_party/EOSSDK/`.

**Epic's samples** are cited throughout as behavioural evidence (paths like
`Samples/Shared/Source/...`). They ship in the same SDK download and land at
`third_party/EOSSDK/Samples/` when you unzip it, so a citation `Samples/X` means
`third_party/EOSSDK/Samples/X`. Like the rest of `third_party/EOSSDK/` they are
Epic's, not ours, and git-ignored. To run them as integration tests see
[Testing against the samples](#testing-against-the-samples).

## The binary contract

Consumers **statically link the import library** `third_party/EOSSDK/SDK/Lib/EOSSDK-Win64-Shipping.lib` and let the loader resolve `EOSSDK-Win64-Shipping.dll` by name from the exe directory. There is no `LoadLibrary`, no `GetProcAddress`, no delay-load anywhere in Epic's samples — file replacement is the only integration mechanism. Consequences:

1. **The DLL filename must match exactly.** `EOSSDK-Win64-Shipping.dll`. (Linux/Mac: `libEOSSDK-Linux-Shipping.so`, `libEOSSDK-Mac-Shipping.dylib`. 1.19 dropped `EOSSDK-Win32-Shipping.dll`.)
2. **A missing export is a process-load failure, not a runtime error.** The game will not start. Completing the export surface therefore comes *before* implementing any behaviour.
3. The shipped Win64 DLL exports **680** symbols. **638** functions are declared via `EOS_DECLARE_FUNC` — 635 in `third_party/EOSSDK/SDK/Include/*.h` and 3 more in the deprecated `.inl` files (`EOS_Achievements_Definition_Release`, `EOS_Achievements_UnlockedAchievement_Release`, `EOS_Leaderboards_LeaderboardDefinition_Release`). Don't scan only `*.h` and miss those. The remaining 42 exports are undocumented internals (`EOS_Audio_*` ×22, `EOS_BroadcastAudio_*` ×15, `EOS_Mercury_{Initialize,Shutdown,Tick}`, `EOS_BeginScopeEvent`, `EOS_EndScopeEvent`) — in no header, so nothing linking against the headers can reference them. (1.16.4's `EOS_RTCVideo_*` internals are gone in 1.19.) **We need exactly the 638.**

### Get the ABI for free — do not redeclare anything

`third_party/EOSSDK/SDK/Include/eos_base.h:65` gates the export macro on a define:

```c
#if defined(EOS_BUILDING_SDK) && EOS_BUILDING_SDK > 0
    #define EOS_API __declspec(dllexport)   // on Windows
```

So **EOSEmu includes Epic's real headers and compiles with `EOS_BUILDING_SDK=1`.** Define each function exactly as the header declares it:

```cpp
EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options) { ... }
```

This makes struct layout, calling convention, and export decoration correct by construction. Do not hand-write prototypes, do not maintain a parallel copy of the headers, and do not write a `.def` file. Specifically it gets these right, all of which are easy to break by hand:

- `#pragma pack(push, 8)` wraps every types header. Layout must match byte-for-byte.
- `EOS_CALL` is empty on x64 (the only platform we build), so Win64 exports are undecorated (`EOS_Initialize`, no `@N` suffix) — matching what the `.lib` expects. (`EOS_CALL` was `__stdcall` on the now-removed Win32 x86 target.)
- `EOS_DECLARE_FUNC` already applies `extern "C"`.

This is verified, not assumed: `tools/check_exports.py` diffs EOSEmu's export table against the functions the headers declare on every build, and the Win64 build exports all 638 undecorated names. (Through 1.16.4 the same check also confirmed the parameter ABI via Win32's `@N` stdcall suffixes; 1.19 dropping Win32 removes that cross-check, but struct-and-signature correctness still comes for free from compiling Epic's own headers with `EOS_BUILDING_SDK=1`.)

## Invariants that will bite

These are the failure modes that are expensive to discover late. Each is verified against the headers or the sample code cited.

**Opaque IDs must be interned.** `EOS_ProductUserId` and `EOS_EpicAccountId` are opaque *pointers* (`eos_common.h:56,107`), not integers — but consumers compare them with raw `==` and order them with `<`. See `Samples/Shared/Source/Core/AccountHelpers.h:98-111`, where the wrapper's `operator==` is `AccountId == Other.AccountId`, and `Friends.cpp:95,221,352`. Therefore: one canonical pointer per distinct ID string, stable for the process lifetime, never freed. `EOS_ProductUserId_FromString("X")` called twice must return the same pointer. An intern table is not an optimisation here; it is a correctness requirement.

**Callbacks must be dispatched from inside `EOS_Platform_Tick`, on the caller's thread.** The header only says Tick "must be called frequently" (`eos_sdk.h`), but the samples settle it: the only `std::mutex` in `Samples/Shared/Source/` is in `Graphics/Console.cpp`, and completion callbacks mutate shared state unlocked (`Friends.cpp:635` → `Friends.push_back` at `:296`). Our LAN transport will run on a background thread; it must **queue** completions and notifications, and drain that queue during `Tick`. Never invoke a consumer callback from a worker thread.

**`ApiVersion` is a real versioning mechanism, not a formality.** Every input `Options` struct begins with `int32_t ApiVersion`. There are 524 `_API_LATEST` macros and they are *not* all `1`:

| Value | Count | Examples |
| --- | --- | --- |
| 1 | 439 | most |
| 2 | 53 | |
| 3 | 22 | `EOS_P2P_SENDPACKET_API_LATEST` |
| 4 | 4 | `EOS_LOBBY_JOINLOBBY_API_LATEST` |
| 5 | 4 | `EOS_INITIALIZE_API_LATEST`, `EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST` |
| 10 | 1 | `EOS_LOBBY_CREATELOBBY_API_LATEST` |
| 15 | 1 | `EOS_PLATFORM_OPTIONS_API_LATEST` |

A game built against an older header passes a **smaller struct** with a lower `ApiVersion`. Reading a field the caller never allocated is an out-of-bounds read. Always branch on `ApiVersion` before touching later fields; if a version is unsupported, return `EOS_IncompatibleVersion` rather than guessing. These `_LATEST` values climb across SDK releases (`CREATELOBBY` was 9 in 1.16.4, 10 here; `INITIALIZE` 4→5; `PLATFORM_OPTIONS` 14→15), so keep the SDK version EOSEmu builds against (in `third_party/EOSSDK/`) aligned with the games under test — a game built on a newer header passes an `ApiVersion` an older EOSEmu would reject. Five headers also `#include` a `*_types_deprecated.inl` carrying legacy struct definitions that must stay compilable.

**`Copy*` allocates, `Get*` borrows.** Anything named `Copy*` transfers ownership via a `T**` out-param and must be freed by its matching `*_Release`. Anything named `Get*` returns a borrowed pointer or a scalar — `GetLobbyOwner`, `GetMemberByIndex` return `EOS_ProductUserId` directly; `Get*Count` returns `uint32_t` with **no result code**. Enumeration is always `GetXCount()` then `CopyXByIndex(i)`.

**Allocators.** `EOS_Initialize` may supply an allocate/reallocate/release triple (`eos_init.h:76-112`); the samples pass `nullptr` for all three. Whatever a `Copy*` allocates, the corresponding `*_Release` must free through the *same* allocator.

**Return-type taxonomy** (getting this wrong is an ABI break): sync ops → `EOS_EResult`; async ops → `void`; counts → `uint32_t`; `AddNotify*` → `EOS_NotificationId` (`uint64_t`, `0` = invalid); `RemoveNotify*` and `*_Release` → `void`.

## What to shim, implement, and stub

**Shim to success — Auth, Connect.** Manufacture a deterministic `EOS_EpicAccountId` / `EOS_ProductUserId` (derive from machine name + a local profile, so IDs are stable across restarts and distinct across machines on the LAN). The samples' default login mode is DevAuth (`Samples/Shared/Source/Graphics/GUI/AuthDialogs.cpp:1350`), so `EOS_Auth_Login` must succeed for `EOS_LCT_Developer` regardless of `Id`/`Token`. Watch this gate: `FAuthentication::ConnectLogin()` (`Samples/Shared/Source/Core/Authentication.cpp:340`) only proceeds if `EOS_Auth_CopyUserAuthToken` returns `EOS_Success` with a non-null `AccessToken`. Shim that too or login stalls silently.

**Implement over LAN — P2P, Lobby, Sessions.** This is the real work. See the design notes below.

**Emulate the overlay with Dear ImGui — UI.** See the section below. This is a late milestone; everything else works with `EOS_UI_*` stubbed.

**Stub everything else** — Achievements, Ecom, Stats, Leaderboards, Storage, RTC, AntiCheat, Mods, KWS, Reports, Sanctions, Metrics, ProgressionSnapshot, IntegratedPlatform. The symbols must exist. Prefer a plausible empty success (`EOS_Success` + zero count) over `EOS_NotImplemented` where a consumer is likely to hard-fail on error; `EOS_NotImplemented` where silence would be misleading. Log every stub hit once, at `EOS_LOG_Warning`, through `EOS_Logging_SetCallback` — the fastest way to learn what a given game actually needs.

Three functions must **not** be stubbed, because the inert value is a lie the caller acts on. They are already implemented in `src/core/`: `EOS_EResult_ToString` (documented never-null; `nullptr` crashes the first error-logging path), `EOS_EResult_IsOperationComplete` (`EOS_FALSE` means "your callback will fire again", so the caller waits forever), and `EOS_GetVersion`.

**AntiCheat is a stub, permanently.** Never attempt real integrity checking, and never implement `EOS_AntiCheat*` in a way that reports a false "clean" verdict to a remote authority. Client-side stubs that satisfy local API calls are fine.

## LAN design notes

Grounded in what the headers actually demand:

**P2P** (`eos_p2p.h`, `eos_p2p_types.h`) is the easiest mapping and the right first target. It's poll-based, not push: the app calls `GetNextReceivedPacketSize` then `ReceivePacket` every frame. There is no `Connect()` — `SendPacket` implicitly opens a connection. Only lifecycle events are callbacks. Constraints: `EOS_P2P_MAX_PACKET_SIZE` = **1170** bytes, `EOS_P2P_MAX_CONNECTIONS` = 32 socket IDs per peer, `SocketName` is 1–32 chars in a restricted charset (`EOS_P2P_SOCKETID_SOCKETNAME_SIZE` = 33 incl. NUL). Three reliability modes (`EOS_PR_UnreliableUnordered`, `ReliableUnordered`, `ReliableOrdered`) plus a `uint8_t` channel per packet — reliability is per-packet, not per-channel, so the reliable layer must ride above the channel demux. Note `EOS_P2P_SendPacket` returning `EOS_Success` means *queued*, not delivered.

Semantics worth reproducing exactly: `PeerConnectionRequest` fires **only** if the connection isn't already accepted, and if no notification is bound for that socket ID the request is silently dropped and later reported as `EOS_CCR_ConnectionIgnored`.

**Lobby** (`eos_lobby.h`) needs real state replication — it has `AddNotifyLobbyUpdateReceived`, `LobbyMemberUpdateReceived`, `LobbyMemberStatusReceived`, so every member must see changes pushed. `EOS_LobbyId` is a `const char*` and globally meaningful. Caps: 16 lobbies, 64 members, 200 search results, 64 attributes. Attributes are two-scoped (lobby-level, owner-only; member-level, self-only) with `EOS_LAT_PUBLIC`/`EOS_LAT_PRIVATE` visibility. Search is by `BucketId` (`"bucket"`), `"mincurrentmembers"`, `"minslotsavailable"`, or arbitrary attributes with `EOS_EComparisonOp`. Host migration and RTC-room coupling exist; RTC can be stubbed, but `bEnableRTCRoom` must not fail the create.

**Sessions** (`eos_sessions.h`) is *structurally different* from Lobby and it's a mistake to treat them as one thing. There is no `CreateSession` — you build an `EOS_HSessionModification` and commit it with `UpdateSession`. Sessions are addressed by a caller-chosen local `const char* SessionName`, distinct from the backend `SessionId`. They have a state machine (`EOS_EOnlineSessionState`, with `StartSession`/`EndSession`), explicit `RegisterPlayers`/`UnregisterPlayers` (cap 1000), and **no update/member notifications at all** — sessions only need to answer searches, not replicate state. `EOS_SessionModification_SetHostAddress` takes an application-defined string; that's the natural place to carry a LAN endpoint.

Both use the same two-phase builder: `Create*Modification` (sync) → mutators (sync, local only) → `Update*` (async, commits) → `*_Release`.

**Discovery**: UDP broadcast or multicast on a fixed port for lobby/session announcement; direct UDP for P2P payloads. Lobby and session search resolve against announcements seen on the LAN. Keep the discovery protocol versioned from day one.

## Overlay emulation (Dear ImGui)

The real EOS overlay is the in-game social panel — the friends list, invite toasts, the profile/report/block dialogs — rendered by injecting into the game's own swapchain. EOSEmu **renders its own overlay with [Dear ImGui](https://github.com/ocornut/imgui)** instead of shipping Epic's overlay binary. This is a late milestone; the networking works with the whole `EOS_UI_*` interface stubbed, so don't block P2P/Lobby/Sessions on it.

What drives it, from the headers:

- **The game asks for the overlay via platform flags**, not a UI call: `EOS_Platform_Options::Flags` carries `EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9` (`0x10`), `_D3D10` (`0x20`), `_OPENGL` (`0x40`) — the samples set all three (`Samples/Shared/Source/Core/Platform.cpp:52`). `EOS_PF_DISABLE_OVERLAY` (`0x02`) / `EOS_PF_DISABLE_SOCIAL_OVERLAY` (`0x04`) opt out. Read these to decide whether to activate at all.
- **`EOS_UI_ShowFriends` / `HideFriends` / `GetFriendsVisible`** are the show/hide entry points — the friends key (`EOS_UI_SetToggleFriendsKey`, default Shift+F3 style combos) toggles the same panel. `ShowFriends` takes a completion callback; visibility changes must also fire `EOS_UI_AddNotifyDisplaySettingsUpdated` observers (games pause/mute when the overlay is up).
- **`EOS_UI_ReportInputState`** is how a game *feeds us input* when it owns the input pump; **`EOS_UI_GetFriendsExclusiveInput`** is how it asks whether the overlay is currently swallowing input. Our ImGui layer is the source of truth for both.
- **`EOS_UI_ShowBlockPlayer` / `ShowReportPlayer` / `ShowNativeProfile`** each pop a specific ImGui modal.

**There is no sanctioned render hook on Windows.** `EOS_UI_PrePresent` looks like one but its header says it is console-only and *"has an empty implementation (i.e. returns `EOS_NotImplemented`) on all non-console platforms."* Stub it to `EOS_NotImplemented` to match. So to draw, the overlay has to get onto the game's present path itself: hook the swapchain (`IDXGISwapChain::Present` for D3D10/11/12, `wglSwapBuffers` for GL, `vkQueuePresentKHR` for Vulkan), stand up an ImGui backend on that device, and render in the hook. This mirrors how the real overlay injects, and it's the load-bearing engineering problem — budget for it accordingly. A windowed-only or separate-window fallback is an acceptable first cut for LAN testing; a game whose overlay never appears still runs.

ImGui and its backends are the first real third-party dependency. It is vendored under `third_party/imgui/` (at a pinned commit) and wired through CMake behind an option so a headless/server build can exclude it.

## Overlay (Dear ImGui)

The social overlay is emulated with **Dear ImGui**. It backs the `EOS_UI` interface (24 functions, `eos_ui.h`): `ShowFriends`/`HideFriends`/`GetFriendsVisible`, `SetToggleFriendsKey`/`Button`, `SetDisplayPreference`/`GetNotificationLocationPreference`, `ShowBlockPlayer`/`ShowReportPlayer`, `PauseSocialOverlay`, `AcknowledgeEventId`, `AddNotifyDisplaySettingsUpdated`.

**There is no sanctioned render hook on desktop.** `EOS_UI_PrePresent` is console-only — the header states it "has an empty implementation (i.e. returns `EOS_NotImplemented`) on all non-console platforms". So on Windows the overlay must hook the game's present path itself, exactly as the real SDK does. That is why the platform flags name graphics APIs at all.

Which API to hook is what the game declares in `EOS_Platform_Options::Flags` (`eos_types.h`):

| Flag | Value |
| --- | --- |
| `EOS_PF_DISABLE_OVERLAY` | `0x00002` |
| `EOS_PF_DISABLE_SOCIAL_OVERLAY` | `0x00004` |
| `EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9` | `0x00010` |
| `EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10` | `0x00020` |
| `EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL` | `0x00040` |

Treat those enable-flags as a **hint, not a contract**. `Samples/Shared/Source/Core/Platform.cpp:52` sets all three while its own comment admits "This sample uses D3D11 or SDL" — no flag exists for D3D11/D3D12/Vulkan. Detect the actual swapchain at runtime; use the flags only to decide whether the game *wants* an overlay at all. Honour `EOS_PF_DISABLE_OVERLAY` and `EOS_PF_DISABLE_SOCIAL_OVERLAY` by installing no hook.

Rules:

- **ImGui stays behind the `EOS_UI` boundary.** No ImGui type may appear in an exported signature and no generated stub TU may include an ImGui header. Vendor it at a pinned commit.
- **Rendering happens on the game's render thread**, inside the hooked present. Callback dispatch does not: `EOS_UI` completion and notification callbacks obey the same rule as every other interface and fire from `EOS_Platform_Tick` on the caller's thread. Do not call back into the game from inside the present hook.
- **Input capture.** When the overlay is visible it may swallow input; that is what `EOS_UI_GetFriendsExclusiveInput` reports and what `AddNotifyDisplaySettingsUpdated` tells the game. Gamepad input arrives via `EOS_UI_ReportInputState`; keyboard/mouse needs a `WndProc` hook. Report exclusive input *only* while actually consuming it, or the game will stop responding.
- `EOS_Initialize_ThreadAffinity` (`eos_init.h`) carries `EmbeddedOverlayMainThread` and `EmbeddedOverlayWorkerThreads` — the real overlay runs its own threads. We need not, but the fields must be accepted without complaint: read them if useful, otherwise ignore them, and never fail `EOS_Initialize` over their values.

EOSEmu builds with CMake ≥ 3.20 and C++17, compiling against `third_party/EOSSDK/SDK/Include` (the default; override with `-DEOSEMU_SDK_INCLUDE_DIR=<path>`):

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Output is `build/Debug/EOSSDK-Win64-Shipping.dll`. A post-build step runs `tools/check_exports.py`, which fails the build if any declared function is missing from the export table — because that would be a host-process *load* failure, not something a test could catch later.

Stubs are generated by `tools/gen_stubs.py` and **committed**, so building never requires Python. To implement a function for real: add its name to `tools/hand_implemented.txt`, re-run the generator, and define it in a hand-written file under `src/`.

## Testing against the samples

Epic's sample apps are the closest thing to a real consumer. They ship in the
EOS SDK download and land at `third_party/EOSSDK/Samples/` when you unzip it
(git-ignored — Epic's, not ours). Build them there, where the paths below are
relative to each sample project, then drop EOSEmu's DLL over the real one in the
sample's output.

The samples build with **VS2017 (toolset v141)**, C++14, via `Samples/Samples.sln`. `Samples/Samples.props` sets the search paths, all relative to each `.vcxproj`:

```
EOSSDKIncludes = ..\..\SDK\Include\
EOSSDKLibs     = ..\..\SDK\Lib\
EOSSDKDLLs     = ..\..\SDK\Bin\
```

Every sample configuration has a post-build step that copies the *real* DLL over the output:

```
xcopy /D /Y /R /Q $(EOSSDKDLLs)EOSSDK-Win64-Shipping.dll $(OutDir) >nul
```

`third_party/EOSSDK/` is reference material, so **do not build into its `SDK/Bin/` and do not disable that xcopy by editing the sample**. `third_party/EOSSDK/SDK/Bin/EOSSDK-Win64-Shipping.dll` is also our behavioural oracle for anything the headers leave ambiguous. Instead, copy our `build/Debug/EOSSDK-Win64-Shipping.dll` into the sample's `OutDir` *after* its build completes — the sample links the import lib and resolves by filename at load time, so a later overwrite is all that's needed. Automate that with a script that writes only into the sample's `OutDir`.

**Two things to know before the first run:**
- Sample credentials are empty by default. `FPlatform::Create()` (`Samples/Shared/Source/Core/Platform.cpp:130-136`) returns `false` *before* ever calling `EOS_Platform_Create` if `ProductId`/`SandboxId`/`DeploymentId` are blank. That gate is in the sample, not the SDK. Pass dummies: `-productid X -sandboxid Y -deploymentid Z`.
- Autologin path: `-autologin -devhost 127.0.0.1:6300 -devcred user`.

**Test ladder, cheapest first:**

1. `AntiCheat/Server` or `Voice/Server` — headless console, no D3D/SDL. Exercises the full `EOS_Initialize` → `EOS_Platform_Create` → `EOS_Platform_Tick` → `EOS_Platform_Release` → `EOS_Shutdown` lifecycle and nothing else. If the process starts, the export surface is complete.
2. `AuthAndFriends` — auth shim + Connect.
3. `P2PNAT` — smallest networking surface: 349 lines, one socket name (`"CHAT"`), channel 0, `EOS_PR_ReliableOrdered`. Never calls `SetRelayControl`, `SetPortRange`, or `SetPacketQueueSize`, so defaults suffice.
4. `Lobbies` — full lobby CRUD, attributes, search, invites, host migration.
5. `SessionMatchmaking` — session lifecycle, registration, search by `BucketId`.

Two instances of (3)/(4)/(5) on one LAN is the acceptance test for the whole project.

## Conventions

- Match Epic's naming at the API boundary (`EOS_Foo_Bar`, `PascalCase` fields) because the headers dictate it. Inside our own code (`src/`), use our own style consistently — don't let Epic's conventions leak into internal code.
- Keep the API boundary a thin translation layer: validate `ApiVersion`, marshal into internal types, hand off. No business logic in the exported function.
- One file per EOS interface (`Auth.cpp`, `P2P.cpp`, `Lobby.cpp`, …) mirroring `third_party/EOSSDK/SDK/Include/eos_*.h`.
- When behaviour is ambiguous, the precedence is: **the header comment** > **what the samples rely on** > the shipped DLL's observed behaviour. Don't guess from the EOS web documentation; this tree is version-pinned at 1.19.1.2 and the docs are not.
- `eos_result.h` defines 251 result codes via an X-macro included into an enum. Return the specific one the header comment names for that function — consumers branch on them (e.g. `EOS_Connect_Login` returning `EOS_InvalidUser` is the documented trigger for the `EOS_Connect_CreateUser` continuation flow, `Authentication.cpp:605`).
