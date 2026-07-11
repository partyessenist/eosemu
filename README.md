# EOSEmu

A drop-in reimplementation of Epic Online Services SDK **1.19.1.2**. It builds to
`EOSSDK-Win64-Shipping.dll` — the same filename a game loads **instead of** Epic's
real one — and satisfies the EOS API surface the game links against, so the game
runs with **no Epic account and no Epic backend**. Auth is shimmed to succeed and
the online backend is replaced with LAN-local transport, so two instances on the
same network discover each other and play together over the local network.

It's for local / LAN play and interop testing of games you own. It does **not**
defeat DRM, anti-cheat, or ownership checks, and does not reach Epic's services.
**64-bit only** (EOS 1.19 dropped Win32).

> Developer/architecture docs (ABI, code generation, interface versioning,
> invariants) live in [`CLAUDE.md`](CLAUDE.md). This file is for **using** the
> emulator to run a game.

---

## Quick start

1. **Get the DLL** — build it (see [Building from source](#building-from-source))
   or use a prebuilt `EOSSDK-Win64-Shipping.dll`.
2. **Back up the game's original** `EOSSDK-Win64-Shipping.dll` (rename it to
   `EOSSDK-Win64-Shipping.orig.dll`), then **replace it** with ours. It sits
   wherever the game loads it from — usually next to the game executable.
3. **Launch the game.** It should start without an Epic account or the Epic
   client. Logins succeed offline with deterministic per-machine IDs.

That's the whole install for a single-player title. For identity, DLC, LAN
multiplayer, and diagnostics, add an `eosemu.ini` (below).

> A platform launcher (or the game's own updater) may restore the real DLL on
> start. Re-check after the first launch, and re-copy if needed.

---

## Configuration — `eosemu.ini`

Drop a file named `eosemu.ini` next to the game's `EOSSDK-Win64-Shipping.dll`.
It's found in this order (higher wins for scalar keys; list keys merge across
sources):

1. the path in the `EOSEMU_CONFIG` env var (explicit),
2. `eosemu.ini` next to the loaded DLL,
3. `<CacheDirectory>/eosemu.ini` — the game's own EOS cache folder.

Format is a small INI: `[sections]`, `key = value`, `#`/`;` comments,
case-insensitive keys, everything optional.
**[`eosemu.ini.example`](eosemu.ini.example) documents every key with examples**
— copy it and edit. The essentials:

| Section / key | Meaning | Default |
|---|---|---|
| `[Identity] DisplayName` | Name shown to LAN peers / in friends lists. | OS user (+ profile) |
| `[Identity] Profile` | Salt so two instances present as distinct peers (see LAN). | none |
| `[Identity] SteamId` | Pin the local user's SteamID64 reported via Connect. | auto-resolved |
| `[Identity] Language` / `Country` | Reported locale / country codes. | `en` / — |
| `[Logging] File` / `Console` / `Level` | EOSEmu's own log sink (see Logging). | off |
| `[Ecom] Entitlement` / `OwnedItem` | Owned DLC / entitlements for ownership checks. | none |
| `[Stats]` / `[Achievements]` | Seed the local user's stat / achievement store. | none |
| `[TitleStorage] Dir` | Folder to serve title-provisioned files from. | — |
| `[Network] DiscoveryPort` | LAN discovery UDP port; isolates LAN groups. | `47100` |

Minimal example:

```ini
[Identity]
DisplayName = Alice

[Ecom]
Entitlement = season_pass:cat_item_001:ent_season
OwnedItem   = cat_item_001
```

---

## Environment variables

| Variable | Effect |
|---|---|
| `EOSEMU_CONFIG` | Full path to the `eosemu.ini` to load — the highest-priority config source. Read when the game creates the EOS platform (at launch). |
| `EOSEMU_PROFILE` | Identity salt: instances with different profiles present as distinct LAN peers (distinct IDs and names) even on one machine / OS user. `[Identity] Profile` overrides it. |
| `EOSEMU_TRACE` | `1` logs every stub call instead of the default once-per-function — the cheapest way to see which unimplemented surface a game leans on. |
| `EOSEMU_OVERLAY_WINDOW` | `1` forces the social overlay into its own window instead of hooking the game's swapchain (useful when the hook misbehaves, or for headless tests). |
| `EOSEMU_OVERLAY_AUTOACCEPT` | `1` auto-accepts incoming overlay invites (test automation). |
| `EOSEMU_OVERLAY_AUTOJOIN` | `1` auto-clicks Join on a friend's joinable presence (test automation). |
| `EOSEMU_P2P_TESTLOSS` | Drops that many per-mille (0–1000) of P2P datagrams to exercise the reliability layer. `300` = 30% loss. Leave unset for real play. |

Boolean variables follow one convention: set unless the value starts with `0`.
`EOSEMU_CONFIG` and `EOSEMU_PROFILE` are read at platform creation, so set them
before launching the game.

---

## LAN multiplayer

Two instances on the same LAN find each other automatically — no configuration
beyond a matching discovery port. Discovery is a UDP broadcast on a fixed port
(`47100` by default; also works over loopback, so two instances on one PC can
play), with an OS-assigned unicast port per process for P2P payloads. It backs:

- **Lobbies** (`EOS_Lobby`) — create / search / join, with lobby + member
  attributes, updates, and invites replicated between instances.
- **Sessions** (`EOS_Sessions`) — create / register / search by bucket id.
- **P2P transport** (`EOS_P2P`) — carries the actual game packets over UDP, with
  a real reliability layer (seq/ack/retransmit/reorder).
- **Presence / Friends** — discovered peers appear as friends so presence-based
  invites/joins work.

**Two machines** is the clean path — distinct identities derive automatically
from machine name + OS user, and each machine has its own global namespace.

**Two instances on one host?** Give each a different `EOSEMU_PROFILE` (or
`[Identity] Profile`) so they present as separate peers; otherwise they derive
the same identity and each drops the other's announcements as its own echo. Many
games also hold a single-instance mutex you'll need to work around.

Peers time out after a few seconds of silence; there's no join handshake that can
fail. If `LAN: up` appears in both logs but they never see each other, it's
almost always **Windows Firewall** blocking UDP on a private network.

---

## Logging & troubleshooting

EOSEmu has its own log sink, independent of whether the game registers an EOS
logging callback — the fastest lever for seeing what a game asks for:

```ini
[Logging]
File  = eosemu_{pid}.log   ; {pid} -> process id, so instances don't clobber
Level = veryverbose        ; each exported call, in order, under its category
```

Levels are `off`/`fatal`/`error`/`warning`/`info`/`verbose`/`veryverbose`. At
`veryverbose` every exported EOS call is traced, so the game's exact call
sequence is readable from the log. **Stub hits are logged once at `warning`** —
anything a game needs that we only stub shows up there first. Set `EOSEMU_TRACE=1`
to log *every* stub call (not just the first) when diagnosing a hang.

Common issues:

- **No `eosemu.log` at all:** the game never called us — wrong DLL replaced, or a
  platform launcher restored the real one. Also check an `EOSEMU_CONFIG` path for
  embedded quotes.
- **Feature greyed out (e.g. co-op menu):** the game may gate it on a
  title-provisioned config file served via Title Storage — see
  below.
- **Two instances look like one peer:** same identity — give each a distinct
  `EOSEMU_PROFILE`.
- **LAN peers never discover each other:** Windows Firewall (UDP / private
  network), different subnets, or mismatched `[Network] DiscoveryPort`.

### Title Storage (live-ops config)

Some games fetch server-hosted config via `EOS_TitleStorage_*` and disable
features when it is missing (e.g.: a co-op menu gated on an
`is_multiplayer_enabled` flag in a config file).

EOSEmu serves these from a folder, resolved as:

1. `[TitleStorage] Dir` in `eosemu.ini` (absolute path recommended), else
2. `<CacheDirectory>/eosemu_titlestorage/`, else
3. `eosemu_titlestorage/` next to the working directory.

Title files are typically **encrypted at rest** in the game's own EOS cache, so
you can't just copy them out. Capture the plaintext once with the **EOSTracer**
proxy during a real online run — it dumps each Title Storage read
to `eostracer_dump/` — then drop those files in the serving folder on every test
machine. Log lines report `QueryFile … found/NOT FOUND` so you can see which
files a game actually asks for.

```ini
[TitleStorage]
Dir = C:\path\to\eosemu_titlestorage
```

---

## Building from source

Requires CMake ≥ 3.20, a C++17 compiler (MSVC on Windows), and **Epic's EOS SDK
headers**, which are **not** included in this repo.

**Get the EOS SDK yourself** — it's **not** in this repo (Epic's license is
Epic's). Download the C SDK from the
[Epic Games Dev Portal](https://onlineservices.epicgames.com/sdk).
Use version **1.19.1.2** to match this build — a mismatched
header set changes struct layouts and `*_API_LATEST` version numbers.

Point the build at it one of two ways:

- **Default location** — unpack the whole SDK archive into `third_party/EOSSDK/`
  (it contains `SDK/`, `Samples/`, `ThirdPartyNotices/`) so that
  `third_party/EOSSDK/SDK/Include/eos_sdk.h` exists (the folder ships with a
  `place_sdk_here.txt` note). The tests additionally link
  `third_party/EOSSDK/SDK/Lib/EOSSDK-Win64-Shipping.lib`. Or,
- **Explicit path** — pass `-DEOSEMU_SDK_INCLUDE_DIR=<path to SDK/Include>`.

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
# or, with the SDK unzipped elsewhere:
cmake -S . -B build -A x64 -DEOSEMU_SDK_INCLUDE_DIR=D:/EOS-SDK/Include
```

Output: `build/Debug/EOSSDK-Win64-Shipping.dll`. Use a **Release** build when
injecting into a shipping game.

The SDK headers are the specification: EOSEmu `#include`s them and compiles with
`EOS_BUILDING_SDK=1`, which flips Epic's own `EOS_API` macro to `dllexport` and
makes struct packing, calling convention, and export decoration correct by
construction. A post-build step diffs the DLL's export table against the headers
— a missing export is a host-process *load* failure, not a runtime error, so it's
treated as a build error.

### Tests

```sh
pwsh tests/run_smoke.ps1     # lifecycle + auth/connect (single process)
pwsh tests/run_lan.ps1       # two-process Lobby/Sessions/P2P acceptance
```

See [`tests/README.md`](tests/README.md) for the full list. The tests link the
SDK import library (`third_party/EOSSDK/SDK/Lib/EOSSDK-Win64-Shipping.lib`).

### Working on it

Stubs are generated from the headers and **committed**, so building never needs
Python. To implement a function for real:

1. Add its name to `tools/hand_implemented.txt`.
2. Re-run `python tools/gen_stubs.py` — the stub disappears.
3. Define it in a hand-written file under `src/`.

Verify without a build:

```sh
python tools/gen_stubs.py --check          # generated sources are current
python tools/check_exports.py <path.dll>   # export table matches the headers
```

Layout:

```
src/generated/   One .gen.cpp per SDK header. Generated, but committed.
src/core/        Runtime: SDK/platform lifecycle, IDs, logging, config, memory.
src/interfaces/  One file per EOS interface (Lobby.cpp, P2P.cpp, ...).
src/net/         LAN transport (UDP discovery + unicast, P2P reliability).
src/overlay/     Dear ImGui overlay + swapchain hook.
docs/            Guides, e.g. lan-coop-testing.md.
tools/           gen_stubs.py, check_exports.py, hand_implemented.txt
third_party/     Vendored ImGui; EOSSDK/ is where you drop Epic's SDK.
```

---

## What works, and what doesn't

Working:

- **Core runtime** — all **638** functions the headers declare are exported and
  verified on every build; consumer-allocator-aware `Copy*`/`*_Release`, ID
  interning, Tick-thread callback dispatch, INI config, dual log sinks.
- **Auth / Connect** — shimmed to succeed with deterministic per-machine IDs; the
  external-account family reports the provider the game logged in with (e.g.
  Steam, with the real SteamID64), not a hardcoded Epic account.
- **P2P / Lobby / Sessions over LAN** — real discovery, lobby state replication,
  session announce/search, and a tested P2P reliability layer.
- **Social** — UserInfo, Friends, Presence, CustomInvites from the LAN peer
  directory.
- **Local state** — Stats, Achievements, PlayerDataStorage with real read-back;
  Title Storage served from a local folder; Ecom entitlements/ownership and
  Sanctions driven by config.
- **Social overlay** — Dear ImGui: hooks the game's D3D11 swapchain to render
  in-game, with a separate-window fallback; backs all 24 `EOS_UI` functions.

Out of scope / limitations:

- **Anything requiring Epic's backend** — cross-internet matchmaking, server-side
  ticket validation, real entitlement/ownership grants. This is LAN/local only by
  design.
- **Proprietary matchmaking backends** — a game whose public server browser routes
  through the publisher's own online service (separate from EOS) can't reach it
  offline; the LAN presence/overlay join is the viable path.
- **AntiCheat** is a permanent stub — it never reports a "clean" verdict to a
  remote authority, only satisfies local API calls.
- Interfaces not listed above (Metrics, Mods, KWS, Reports, RTC, …) exist as stubs
  that log once and return a plausible inert value.

Behavior for any given game depends on which interfaces and versions it uses. The
three functions a stub would actively break — `EOS_EResult_ToString`,
`EOS_EResult_IsOperationComplete`, `EOS_GetVersion` — are byte-for-byte
indistinguishable from Epic's shipped DLL.

---

## Legal / scope

For running and testing games **you own**, locally or on your LAN. Don't use it to
circumvent DRM / anti-cheat on titles you aren't authorized to run, or to enable
piracy. EOSEmu ships none of Epic's code — build it against headers you download
yourself from Epic under their SDK license.
