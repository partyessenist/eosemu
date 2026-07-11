#pragma once

//
// Mods interface -- installed/available mods served from config.
//
// EOSEmu models no mod marketplace, but a mod-aware game enumerates installed
// mods at boot and hard-fails (or shows an errored list) when the enumeration
// completes NotImplemented. The [Mods] section of eosemu.ini declares the mods
// the local user "has", and this interface answers EnumerateMods/CopyModInfo
// from that declaration. Install/Uninstall/Update succeed and mutate the
// in-memory installed set only -- nothing is downloaded. See HANDOFF.md,
// "P1 -- Mods".
//
// Config is parsed lazily on first use so a [Mods] block supplied via the
// CacheDirectory eosemu.ini (loaded after this object is constructed) still
// takes effect -- same rationale as EcomInterface.
//

#include <mutex>
#include <string>
#include <vector>

#include "interfaces/Interfaces.h"

#include "eos_mods_types.h"

namespace EOSEmu
{
	class ModsInterface : public InterfaceBase
	{
	public:
		explicit ModsInterface(Platform& Owner) : InterfaceBase(Owner) {}

		struct Mod
		{
			std::string NamespaceId;
			std::string ItemId;
			std::string ArtifactId;
			std::string Title;
			std::string Version;
			bool Installed = true; // config mods start installed
		};

		// Snapshot of mods for one enumeration type (installed subset or all).
		std::vector<Mod> Snapshot(EOS_EModEnumerationType Type);

		// EnumerateMods must run before CopyModInfo of that type answers -- the
		// header documents EOS_NotFound for a type never enumerated.
		void MarkEnumerated(EOS_EModEnumerationType Type);
		bool WasEnumerated(EOS_EModEnumerationType Type);

		// Install accepts any identifier (marking a config mod installed or
		// appending an unknown one); Uninstall/Update return false when no mod
		// with that ItemId exists.
		void Install(const Mod& M);
		bool Uninstall(const std::string& ItemId);
		bool Exists(const std::string& ItemId);

	private:
		void EnsureLoaded();

		std::mutex Mutex_;
		bool Loaded_ = false;
		bool EnumeratedInstalled_ = false;
		bool EnumeratedAll_ = false;
		std::vector<Mod> Mods_;
	};
}
