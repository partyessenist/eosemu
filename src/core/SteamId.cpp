#include "core/SteamId.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace EOSEmu
{
	namespace SteamId
	{
		bool IsIndividualId(uint64_t Id)
		{
			// SteamID64 packs [63:56] universe, [55:52] account type, [51:32]
			// instance, [31:0] account number.
			return (Id >> 56) == 1 && ((Id >> 52) & 0xF) == 1 && (Id & 0xFFFFFFFFull) != 0;
		}

		namespace
		{
			int HexNibble(char C)
			{
				if (C >= '0' && C <= '9') return C - '0';
				if (C >= 'a' && C <= 'f') return C - 'a' + 10;
				if (C >= 'A' && C <= 'F') return C - 'A' + 10;
				return -1;
			}

			uint32_t ReadLE32(const uint8_t* P)
			{
				return static_cast<uint32_t>(P[0]) | (static_cast<uint32_t>(P[1]) << 8) |
					(static_cast<uint32_t>(P[2]) << 16) | (static_cast<uint32_t>(P[3]) << 24);
			}

			uint64_t ReadLE64(const uint8_t* P)
			{
				return static_cast<uint64_t>(ReadLE32(P)) |
					(static_cast<uint64_t>(ReadLE32(P + 4)) << 32);
			}
		}

		uint64_t FromSessionTicketHex(const char* Token)
		{
			if (Token == nullptr)
			{
				return 0;
			}
			const size_t HexLen = std::strlen(Token);
			// The ticket opens with a 24-byte GC-token section (decoded below);
			// anything shorter cannot be one. Odd length is not valid hex.
			if (HexLen < 48 || (HexLen % 2) != 0)
			{
				return 0;
			}
			std::vector<uint8_t> Bytes(HexLen / 2);
			for (size_t i = 0; i < Bytes.size(); ++i)
			{
				const int High = HexNibble(Token[2 * i]);
				const int Low = HexNibble(Token[2 * i + 1]);
				if (High < 0 || Low < 0)
				{
					return 0;
				}
				Bytes[i] = static_cast<uint8_t>((High << 4) | Low);
			}
			// An auth session ticket begins with the GC token section:
			//   uint32 SectionLength (== 20)
			//   uint64 GCToken
			//   uint64 SteamID64
			//   uint32 GenerationTime
			// Real Steam and the common emulators all emit this layout. An
			// encrypted app ticket (or a future format change) fails the length
			// prefix or the id-shape check and falls through to 0.
			if (ReadLE32(Bytes.data()) != 20)
			{
				return 0;
			}
			const uint64_t Id = ReadLE64(Bytes.data() + 12);
			return IsIndividualId(Id) ? Id : 0;
		}

		uint64_t QueryLoadedSteamApi()
		{
#if defined(_WIN32)
			// Resolved dynamically: EOSEmu must not link the Steamworks SDK, and
			// the module is only present when the game itself brought one along.
			HMODULE Mod = GetModuleHandleW(L"steam_api64.dll");
			if (Mod == nullptr)
			{
				return 0;
			}
			using GetHSteamUserFn = int32_t (*)();
			using FindOrCreateUserInterfaceFn = void* (*)(int32_t, const char*);
			using GetSteamIDFn = uint64_t (*)(void*);
			auto GetUser = reinterpret_cast<GetHSteamUserFn>(
				GetProcAddress(Mod, "SteamAPI_GetHSteamUser"));
			auto FindInterface = reinterpret_cast<FindOrCreateUserInterfaceFn>(
				GetProcAddress(Mod, "SteamInternal_FindOrCreateUserInterface"));
			auto GetSteamID = reinterpret_cast<GetSteamIDFn>(
				GetProcAddress(Mod, "SteamAPI_ISteamUser_GetSteamID"));
			if (GetUser == nullptr || FindInterface == nullptr || GetSteamID == nullptr)
			{
				return 0;
			}
			const int32_t User = GetUser();
			if (User == 0)
			{
				// SteamAPI_Init has not run yet; a later call may succeed.
				return 0;
			}
			// ISteamUser versions, newest first. GetSteamID's vtable slot is the
			// same in all of them, so any version the module recognises will do.
			static const char* const Versions[] = {
				"SteamUser023", "SteamUser022", "SteamUser021", "SteamUser020",
				"SteamUser019", "SteamUser018", "SteamUser017",
			};
			for (const char* Version : Versions)
			{
				if (void* Iface = FindInterface(User, Version))
				{
					const uint64_t Id = GetSteamID(Iface);
					if (IsIndividualId(Id))
					{
						return Id;
					}
				}
			}
#endif
			return 0;
		}
	}
}
