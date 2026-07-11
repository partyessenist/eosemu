#include "core/Identity.h"

#include "core/Config.h"
#include "core/Ids.h"

#include <cstdint>
#include <cstdlib>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <lmcons.h>
#else
#	include <unistd.h>
#	include <pwd.h>
#endif

namespace EOSEmu
{
	namespace
	{
		// 64-bit FNV-1a. Enough spread for a stable per-machine identifier; this
		// is a name derivation, not a security primitive.
		uint64_t Fnv1a(const std::string& Data, uint64_t Seed)
		{
			uint64_t Hash = Seed;
			for (unsigned char C : Data)
			{
				Hash ^= C;
				Hash *= 1099511628211ull;
			}
			return Hash;
		}

		std::string MachineName()
		{
#if defined(_WIN32)
			char Buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
			DWORD Size = sizeof(Buffer);
			if (GetComputerNameA(Buffer, &Size) && Size > 0)
			{
				return std::string(Buffer, Size);
			}
#else
			char Buffer[256] = {};
			if (gethostname(Buffer, sizeof(Buffer) - 1) == 0)
			{
				return Buffer;
			}
#endif
			return "unknown-host";
		}

		std::string UserName()
		{
#if defined(_WIN32)
			char Buffer[UNLEN + 1] = {};
			DWORD Size = sizeof(Buffer);
			if (GetUserNameA(Buffer, &Size) && Size > 1)
			{
				// Size includes the null terminator here.
				return std::string(Buffer, Size - 1);
			}
#else
			if (const char* Env = std::getenv("USER"))
			{
				return Env;
			}
			if (struct passwd* Pw = getpwuid(getuid()))
			{
				return Pw->pw_name;
			}
#endif
			return "player";
		}
	}

	std::string DeriveIdHex(const std::string& Seed)
	{
		// Two independent 64-bit hashes concatenated give the 32 hex chars a
		// real EOS ID uses. Different offset-bases keep the two halves from
		// collapsing for short seeds.
		const uint64_t High = Fnv1a(Seed, 14695981039346656037ull);
		const uint64_t Low = Fnv1a(Seed, 1469598103934665603ull);
		char Out[33];
		std::snprintf(Out, sizeof(Out), "%016llx%016llx",
			static_cast<unsigned long long>(High),
			static_cast<unsigned long long>(Low));
		return std::string(Out, 32);
	}

	namespace
	{
		// Optional profile salt. Lets two EOSEmu processes on the *same* machine
		// (same OS user) present as distinct peers -- essential for running two
		// game instances on one dev box, and for the two-instance LAN tests.
		std::string ProfileSalt()
		{
#if defined(_WIN32)
			char Buffer[128] = {};
			size_t Length = 0;
			if (getenv_s(&Length, Buffer, sizeof(Buffer), "EOSEMU_PROFILE") == 0 && Length > 1)
			{
				return std::string(Buffer, Length - 1);
			}
#else
			if (const char* Env = std::getenv("EOSEMU_PROFILE"))
			{
				return Env;
			}
#endif
			return {};
		}
	}

	Identity::Identity(const Config* Cfg)
	{
		const std::string Machine = MachineName();
		const std::string User = UserName();
		// [Identity] Profile overrides the EOSEMU_PROFILE env var (same effect).
		std::string Profile = ProfileSalt();
		if (Cfg != nullptr)
		{
			const std::string CfgProfile = Cfg->GetString("Identity", "Profile");
			if (!CfgProfile.empty()) Profile = CfgProfile;
		}
		const std::string Base = Machine + "|" + User + (Profile.empty() ? "" : "|" + Profile);

		// Salt the two IDs apart so a machine's Epic and Product IDs differ,
		// as they do in the real ecosystem.
		EpicIdStr_ = DeriveIdHex("epic|" + Base);
		ProductIdStr_ = DeriveIdHex("product|" + Base);
		DisplayName_ = (User.empty() ? Machine : User) + (Profile.empty() ? "" : "-" + Profile);

		// Config overrides, applied last so they win over the derived values.
		if (Cfg != nullptr)
		{
			const std::string CfgName = Cfg->GetString("Identity", "DisplayName");
			if (!CfgName.empty()) DisplayName_ = CfgName;
			const std::string CfgEpic = Cfg->GetString("Identity", "EpicAccountId");
			if (!CfgEpic.empty()) EpicIdStr_ = CfgEpic;
			const std::string CfgProduct = Cfg->GetString("Identity", "ProductUserId");
			if (!CfgProduct.empty()) ProductIdStr_ = CfgProduct;
		}

		EpicId_ = Ids::InternEpic(EpicIdStr_);
		ProductId_ = Ids::InternProduct(ProductIdStr_);
	}
}
