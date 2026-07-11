# EOSEmu tests

Acceptance tests, cheapest first. All link Epic's import library
`third_party/EOSSDK/SDK/Lib/EOSSDK-Win64-Shipping.lib` and resolve
`EOSSDK-Win64-Shipping.dll` (EOSEmu's) by name at load time — the same
integration path a shipped game uses. See the top-level README for where to put
Epic's SDK.

## 1. Smoke test — lifecycle + auth/connect (single process)

```
cmake --build build --config Debug
pwsh tests/run_smoke.ps1
```

Drives `EOS_Initialize → Platform_Create → Auth_Login → Connect_Login → Tick →
Platform_Release → Shutdown` and checks the deterministic IDs come back. Expect
`SMOKE PASS`.

## 2. Two-instance LAN test — Lobby + Sessions + P2P (two processes)

```
pwsh tests/run_lan.ps1
```

Launches a `host` and a `join` process on this machine with distinct
`EOSEMU_PROFILE` values (so they present as separate peers). The host creates a
lobby and advertises a session; the joiner discovers the host, searches and
joins the lobby, finds the session, and exchanges a P2P packet. Expect
`LAN ACCEPTANCE: PASS`.

This is the acceptance test for the networked interfaces — it exercises real
discovery, lobby state replication, session announcement/search, and the P2P
send/receive/accept path across two OS processes.

## Focused suites (single or two process)

```
pwsh tests/run_config.ps1        # eosemu.ini -> identity/locale/Ecom/stats/sanctions + emu log sink
pwsh tests/run_steamid.ps1       # real SteamID64 via Connect ExternalAccountInfo: parsed from the
                                 #   login session ticket, pinned by [Identity] SteamId, or queried
                                 #   live from a loaded steam_api64.dll (fake module fixture)
pwsh tests/run_overlay.ps1       # EOS_UI show/hide/notify/keys/pause + overlay-disabled path
pwsh tests/run_hook.ps1          # in-game swapchain hook: a stand-in D3D11 game presents frames;
                                 #   assert the overlay hooks Present, captures the swapchain,
                                 #   renders while visible, and unhooks cleanly on Release
pwsh tests/run_p2p_reliable.ps1  # 120 ReliableOrdered packets through 30% induced loss
pwsh tests/run_p2p_edge.ps1      # queue-full backpressure (no reliable loss), ConnectionIgnored,
                                 #   PeerConnectionInterrupted -> Closed(TimedOut) on a vanished peer
pwsh tests/run_hostmig.ps1       # owner leaves -> remaining member promoted (EOS_LMS_PROMOTED) + adopts
pwsh tests/run_storage.ps1       # PlayerDataStorage: request lifetime safety, filename validation,
                                 #   [Storage] Persist=true disk mirror surviving a process restart
pwsh tests/run_invite.ps1        # overlay invite-accept + join-friend (UiEventId) across two processes
pwsh tests/run_reports.ps1       # player report -> Success completion; invalid -> InvalidParameters
pwsh tests/run_mods.ps1          # [Mods] config list through enumerate/copy/install/uninstall/update
pwsh tests/run_rtc.ps1           # RTC shim: lobby-coupled room connect/participants/mute round-trip,
                                 #   AccessDenied on manual lobby-room join, custom rooms, fake devices,
                                 #   SendData validation, disconnect on lobby leave
```

## 3. Real Epic samples — export surface + lifecycle

The samples target the VS2017 (v141) toolset and the 10.0.17763 Windows SDK. If
those aren't installed, `build_sample.ps1` overrides the toolset/SDK on the
command line (no project edits):

```
# Headless — exercises the full lifecycle and nothing else. If it starts, the
# export surface is complete (a missing export is a load-time failure).
pwsh tests/build_sample.ps1 -Project ..\..\Samples\AntiCheat\Server\AntiCheatServer.vcxproj `
     -Config Debug -OutSubdir Bin\Win64\Debug
& ..\..\Samples\AntiCheat\Server\Bin\Win64\Debug\AntiCheatServer.exe -productid X -sandboxid Y -deploymentid Z
```

`deploy_to_sample.ps1` copies EOSEmu's DLL over the real one in a sample's
output directory (run after the sample's own post-build xcopy). `build_sample.ps1`
does the build + deploy in one step.

Two instances of `P2PNAT`, `Lobbies`, or `SessionMatchmaking` on one LAN is the
end-to-end acceptance test for the whole project; those are DirectX/SDL GUI apps
and need a display to run interactively.
