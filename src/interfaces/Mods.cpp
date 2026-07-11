//
// Mods interface -- mod list from config; install state in memory only.
//
// EnumerateMods posts Success and unlocks CopyModInfo for that type;
// CopyModInfo builds the EOS_Mods_ModInfo array from the [Mods] block of
// eosemu.ini. Install/Uninstall/Update succeed against the in-memory set.
// See Mods.h and HANDOFF.md, "P1 -- Mods".
//

#include "interfaces/Mods.h"
#include "interfaces/Common.h"

#include "core/Config.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_mods.h"

namespace EOSEmu
{
	namespace
	{
		// Splits "id:title:version[:artifactId]" -- same colon syntax as [Ecom].
		std::vector<std::string> SplitColon(const std::string& S)
		{
			std::vector<std::string> Parts;
			size_t Start = 0;
			while (true)
			{
				const size_t Colon = S.find(':', Start);
				if (Colon == std::string::npos)
				{
					Parts.push_back(S.substr(Start));
					break;
				}
				Parts.push_back(S.substr(Start, Colon - Start));
				Start = Colon + 1;
			}
			return Parts;
		}
	}

	void ModsInterface::EnsureLoaded()
	{
		// Caller holds Mutex_.
		if (Loaded_) return;
		Loaded_ = true;

		Config& Cfg = Platform_.Config();
		const std::string Namespace = Cfg.GetString("Mods", "Namespace", "eosemu");
		for (const std::string& Raw : Cfg.GetList("Mods", "Mod"))
		{
			const std::vector<std::string> Parts = SplitColon(Raw);
			if (Parts.empty() || Parts[0].empty()) continue;
			Mod M;
			M.NamespaceId = Namespace;
			M.ItemId = Parts[0];
			M.Title = (Parts.size() > 1 && !Parts[1].empty()) ? Parts[1] : Parts[0];
			M.Version = (Parts.size() > 2 && !Parts[2].empty()) ? Parts[2] : "1.0.0";
			M.ArtifactId = (Parts.size() > 3 && !Parts[3].empty()) ? Parts[3] : Parts[0];
			Mods_.push_back(std::move(M));
		}

		if (!Mods_.empty())
		{
			EOSEMU_INFO(Mods, "Mods config: %zu mod(s) installed", Mods_.size());
		}
	}

	std::vector<ModsInterface::Mod> ModsInterface::Snapshot(EOS_EModEnumerationType Type)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		std::vector<Mod> Out;
		for (const Mod& M : Mods_)
		{
			if (Type == EOS_EModEnumerationType::EOS_MET_INSTALLED && !M.Installed) continue;
			Out.push_back(M);
		}
		return Out;
	}

	void ModsInterface::MarkEnumerated(EOS_EModEnumerationType Type)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		if (Type == EOS_EModEnumerationType::EOS_MET_INSTALLED) EnumeratedInstalled_ = true;
		else EnumeratedAll_ = true;
	}

	bool ModsInterface::WasEnumerated(EOS_EModEnumerationType Type)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		return Type == EOS_EModEnumerationType::EOS_MET_INSTALLED ? EnumeratedInstalled_ : EnumeratedAll_;
	}

	void ModsInterface::Install(const Mod& M)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		for (Mod& Existing : Mods_)
		{
			if (Existing.ItemId == M.ItemId)
			{
				Existing.Installed = true;
				return;
			}
		}
		Mods_.push_back(M);
		Mods_.back().Installed = true;
	}

	bool ModsInterface::Uninstall(const std::string& ItemId)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		for (Mod& Existing : Mods_)
		{
			if (Existing.ItemId == ItemId && Existing.Installed)
			{
				Existing.Installed = false;
				return true;
			}
		}
		return false;
	}

	bool ModsInterface::Exists(const std::string& ItemId)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		for (const Mod& Existing : Mods_)
		{
			if (Existing.ItemId == ItemId) return true;
		}
		return false;
	}

	namespace
	{
		// Deep-copies the caller's identifier: the Options struct is only valid for
		// the duration of the call, but the completion fires on a later Tick.
		ModsInterface::Mod CopyIdentifier(const EOS_Mod_Identifier* Id)
		{
			ModsInterface::Mod M;
			if (Id == nullptr) return M;
			M.NamespaceId = Id->NamespaceId ? Id->NamespaceId : "";
			M.ItemId = Id->ItemId ? Id->ItemId : "";
			M.ArtifactId = Id->ArtifactId ? Id->ArtifactId : "";
			M.Title = Id->Title ? Id->Title : "";
			M.Version = Id->Version ? Id->Version : "";
			return M;
		}

		// Borrowed identifier rooted in a Mod snapshot that outlives the callback.
		EOS_Mod_Identifier BorrowIdentifier(const ModsInterface::Mod& M)
		{
			EOS_Mod_Identifier Id = {};
			Id.ApiVersion = EOS_MOD_IDENTIFIER_API_LATEST;
			Id.NamespaceId = M.NamespaceId.c_str();
			Id.ItemId = M.ItemId.c_str();
			Id.ArtifactId = M.ArtifactId.c_str();
			Id.Title = M.Title.c_str();
			Id.Version = M.Version.c_str();
			return Id;
		}
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Mods_InstallMod(EOS_HMods Handle, const EOS_Mods_InstallModOptions* Options, void* ClientData, const EOS_Mods_OnInstallModCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ModsInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	ModsInterface::Mod M = Options ? CopyIdentifier(Options->Mod) : ModsInterface::Mod{};
	const bool HaveMod = Options != nullptr && Options->Mod != nullptr && !M.ItemId.empty();
	if (Options == nullptr || !HaveMod)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_MODS_INSTALLMOD_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else
	{
		I->Install(M);
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, M, HaveMod]
	{
		EOS_Mod_Identifier Id = BorrowIdentifier(M);
		EOS_Mods_InstallModCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.LocalUserId = Local;
		Info.ClientData = ClientData;
		Info.Mod = HaveMod ? &Id : nullptr;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Mods_UninstallMod(EOS_HMods Handle, const EOS_Mods_UninstallModOptions* Options, void* ClientData, const EOS_Mods_OnUninstallModCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ModsInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	ModsInterface::Mod M = Options ? CopyIdentifier(Options->Mod) : ModsInterface::Mod{};
	const bool HaveMod = Options != nullptr && Options->Mod != nullptr && !M.ItemId.empty();
	if (Options == nullptr || !HaveMod)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_MODS_UNINSTALLMOD_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->Uninstall(M.ItemId))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, M, HaveMod]
	{
		EOS_Mod_Identifier Id = BorrowIdentifier(M);
		EOS_Mods_UninstallModCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.LocalUserId = Local;
		Info.ClientData = ClientData;
		Info.Mod = HaveMod ? &Id : nullptr;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Mods_EnumerateMods(EOS_HMods Handle, const EOS_Mods_EnumerateModsOptions* Options, void* ClientData, const EOS_Mods_OnEnumerateModsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ModsInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	EOS_EModEnumerationType Type = Options ? Options->Type : EOS_EModEnumerationType::EOS_MET_INSTALLED;
	if (Options == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_MODS_ENUMERATEMODS_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else
	{
		I->MarkEnumerated(Type);
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Type]
	{
		EOS_Mods_EnumerateModsCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.LocalUserId = Local;
		Info.ClientData = ClientData;
		Info.Type = Type;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Mods_CopyModInfo(EOS_HMods Handle, const EOS_Mods_CopyModInfoOptions* Options, EOS_Mods_ModInfo** OutEnumeratedMods)
{
	EOSEMU_API_TRACE();
	auto* I = As<ModsInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutEnumeratedMods == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutEnumeratedMods = nullptr;
	if (!VersionInRange(Options->ApiVersion, EOS_MODS_COPYMODINFO_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	// Documented: NotFound when this enumeration type was never performed.
	if (!I->WasEnumerated(Options->Type)) return EOS_EResult::EOS_NotFound;

	const std::vector<ModsInterface::Mod> Mods = I->Snapshot(Options->Type);
	EOS_Mods_ModInfo* Out = AllocApi<EOS_Mods_ModInfo>();
	Out->ApiVersion = EOS_MODS_MODINFO_API_LATEST;
	Out->Type = Options->Type;
	Out->ModsCount = static_cast<int32_t>(Mods.size());
	Out->Mods = AttachArray<EOS_Mod_Identifier>(Out, Mods.size());
	for (size_t i = 0; i < Mods.size(); ++i)
	{
		EOS_Mod_Identifier& Id = Out->Mods[i];
		Id.ApiVersion = EOS_MOD_IDENTIFIER_API_LATEST;
		Id.NamespaceId = AttachString(Out, Mods[i].NamespaceId.c_str());
		Id.ItemId = AttachString(Out, Mods[i].ItemId.c_str());
		Id.ArtifactId = AttachString(Out, Mods[i].ArtifactId.c_str());
		Id.Title = AttachString(Out, Mods[i].Title.c_str());
		Id.Version = AttachString(Out, Mods[i].Version.c_str());
	}
	*OutEnumeratedMods = Out;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Mods_UpdateMod(EOS_HMods Handle, const EOS_Mods_UpdateModOptions* Options, void* ClientData, const EOS_Mods_OnUpdateModCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ModsInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	ModsInterface::Mod M = Options ? CopyIdentifier(Options->Mod) : ModsInterface::Mod{};
	const bool HaveMod = Options != nullptr && Options->Mod != nullptr && !M.ItemId.empty();
	if (Options == nullptr || !HaveMod)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_MODS_UPDATEMOD_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->Exists(M.ItemId))
	{
		Result = EOS_EResult::EOS_NotFound;
	}
	// An existing mod is already "up to date", which the header documents as a
	// successful completion.

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, M, HaveMod]
	{
		EOS_Mod_Identifier Id = BorrowIdentifier(M);
		EOS_Mods_UpdateModCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.LocalUserId = Local;
		Info.ClientData = ClientData;
		Info.Mod = HaveMod ? &Id : nullptr;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Mods_ModInfo_Release(EOS_Mods_ModInfo* ModInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(ModInfo);
}
