// Minimal stand-in for steam_api64.dll, used by run_steamid.ps1.
//
// Exports just the three flat-API entry points EOSEmu's SteamId::
// QueryLoadedSteamApi resolves, answering with a fixed SteamID64 -- the same
// contract a real steam_api64.dll (or a Steam emulator) fulfils. steamid.c
// loads it by name from its own directory before creating the platform.
//
// The id here must match EXPECT_MODULE_ID in steamid.c.

#include <stdint.h>

#define FAKE_STEAM_ID 76561197971234567ULL

static int g_FakeInterface;

__declspec(dllexport) int32_t SteamAPI_GetHSteamUser(void)
{
	return 1;
}

__declspec(dllexport) void* SteamInternal_FindOrCreateUserInterface(int32_t User, const char* Version)
{
	(void)User;
	(void)Version;
	return &g_FakeInterface;
}

__declspec(dllexport) uint64_t SteamAPI_ISteamUser_GetSteamID(void* Interface)
{
	(void)Interface;
	return FAKE_STEAM_ID;
}
