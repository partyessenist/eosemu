//
// Achievements interface -- real in-memory unlock tracking with read-back.
//
// There is no backend achievement catalog on the LAN, so definition queries
// report an empty (but successful) catalog. UnlockAchievements records the
// unlock per user, fires the unlocked notifications, and is reflected by the
// player/unlocked counts and Copy* accessors. Process-global state; the handle
// sentinel is ignored and dispatch routes through the live platform.
//

#include "interfaces/Interfaces.h"
#include "core/Logging.h"
#include "interfaces/Common.h"
#include "interfaces/LocalStores.h"

#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_achievements_types.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace EOSEmu
{
	namespace
	{
		constexpr int64_t kUnlockTime = 1700000000; // fixed, deterministic POSIX time

		std::mutex g_Mutex;
		// productId -> ordered set of unlocked achievement ids
		std::map<std::string, std::vector<std::string>> g_Unlocked;

		struct NotifyV2 { EOS_NotificationId Id; EOS_Achievements_OnAchievementsUnlockedCallbackV2 Fn; void* ClientData; };
		struct NotifyV1 { EOS_NotificationId Id; EOS_Achievements_OnAchievementsUnlockedCallback Fn; void* ClientData; };
		std::vector<NotifyV2> g_NotifyV2;
		std::vector<NotifyV1> g_NotifyV1;

		Dispatcher* Dispatch()
		{
			Platform* P = CurrentPlatform();
			return P ? &P->Dispatch() : nullptr;
		}

		std::vector<std::string>* Find(const std::string& Pid)
		{
			auto It = g_Unlocked.find(Pid);
			return It != g_Unlocked.end() ? &It->second : nullptr;
		}
	}

	void SeedUnlockedAchievement(const std::string& ProductId, const std::string& AchievementId)
	{
		if (ProductId.empty() || AchievementId.empty()) return;
		std::lock_guard<std::mutex> Lock(g_Mutex);
		auto& V = g_Unlocked[ProductId];
		for (const auto& E : V) if (E == AchievementId) return; // already present
		V.push_back(AchievementId);
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Achievements_QueryDefinitions(EOS_HAchievements Handle, const EOS_Achievements_QueryDefinitionsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryDefinitionsCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	D->Post([CompletionDelegate, ClientData]
	{
		EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Achievements_GetAchievementDefinitionCount(EOS_HAchievements Handle, const EOS_Achievements_GetAchievementDefinitionCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return 0; // empty catalog
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyAchievementDefinitionV2ByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyAchievementDefinitionV2ByIndexOptions* Options, EOS_Achievements_DefinitionV2** OutDefinition)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutDefinition) *OutDefinition = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyAchievementDefinitionV2ByAchievementId(EOS_HAchievements Handle, const EOS_Achievements_CopyAchievementDefinitionV2ByAchievementIdOptions* Options, EOS_Achievements_DefinitionV2** OutDefinition)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutDefinition) *OutDefinition = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Achievements_QueryPlayerAchievements(EOS_HAchievements Handle, const EOS_Achievements_QueryPlayerAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_ProductUserId Target = Options ? Options->TargetUserId : nullptr;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	D->Post([CompletionDelegate, ClientData, Target, Local]
	{
		EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.TargetUserId = Target;
		Info.LocalUserId = Local;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Achievements_GetPlayerAchievementCount(EOS_HAchievements Handle, const EOS_Achievements_GetPlayerAchievementCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || Options->UserId == nullptr) return 0;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto* V = Find(Ids::ProductString(Options->UserId));
	return V ? static_cast<uint32_t>(V->size()) : 0;
}

namespace
{
	EOS_Achievements_PlayerAchievement* MakePlayerAchievement(const std::string& Id)
	{
		EOS_Achievements_PlayerAchievement* A = AllocApi<EOS_Achievements_PlayerAchievement>();
		A->ApiVersion = EOS_ACHIEVEMENTS_PLAYERACHIEVEMENT_API_LATEST;
		A->AchievementId = AttachString(A, Id.c_str());
		A->Progress = 100.0;
		A->UnlockTime = kUnlockTime;
		A->StatInfoCount = 0;
		A->StatInfo = nullptr;
		A->DisplayName = AttachString(A, Id.c_str());
		A->Description = AttachString(A, "");
		A->IconURL = AttachString(A, "");
		A->FlavorText = AttachString(A, "");
		return A;
	}
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyPlayerAchievementByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyPlayerAchievementByIndexOptions* Options, EOS_Achievements_PlayerAchievement** OutAchievement)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutAchievement == nullptr || Options->TargetUserId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutAchievement = nullptr;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto* V = Find(Ids::ProductString(Options->TargetUserId));
	if (!V || Options->AchievementIndex >= V->size()) return EOS_EResult::EOS_NotFound;
	*OutAchievement = MakePlayerAchievement((*V)[Options->AchievementIndex]);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyPlayerAchievementByAchievementId(EOS_HAchievements Handle, const EOS_Achievements_CopyPlayerAchievementByAchievementIdOptions* Options, EOS_Achievements_PlayerAchievement** OutAchievement)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutAchievement == nullptr || Options->TargetUserId == nullptr || Options->AchievementId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutAchievement = nullptr;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto* V = Find(Ids::ProductString(Options->TargetUserId));
	if (!V) return EOS_EResult::EOS_NotFound;
	for (const auto& Id : *V) if (Id == Options->AchievementId) { *OutAchievement = MakePlayerAchievement(Id); return EOS_EResult::EOS_Success; }
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Achievements_UnlockAchievements(EOS_HAchievements Handle, const EOS_Achievements_UnlockAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
	uint32_t Count = 0;
	EOS_ProductUserId User = nullptr;
	std::vector<std::string> Newly;
	if (Options != nullptr && Options->UserId != nullptr)
	{
		User = Options->UserId;
		const std::string Pid = Ids::ProductString(User);
		std::lock_guard<std::mutex> Lock(g_Mutex);
		auto& V = g_Unlocked[Pid];
		for (uint32_t i = 0; Options->AchievementIds != nullptr && i < Options->AchievementsCount; ++i)
		{
			if (Options->AchievementIds[i] == nullptr) continue;
			const std::string Id = Options->AchievementIds[i];
			bool Present = false;
			for (const auto& E : V) if (E == Id) { Present = true; break; }
			if (!Present) { V.push_back(Id); Newly.push_back(Id); }
		}
		Count = Options->AchievementsCount;
		Result = EOS_EResult::EOS_Success;
	}

	Dispatcher* D = Dispatch();
	if (D != nullptr)
	{
		// Fire per-achievement unlocked notifications (both V2 and deprecated).
		std::vector<NotifyV2> V2; std::vector<NotifyV1> V1;
		{
			std::lock_guard<std::mutex> Lock(g_Mutex);
			V2 = g_NotifyV2; V1 = g_NotifyV1;
		}
		for (const std::string& Id : Newly)
		{
			for (const auto& N : V2)
			{
				auto Fn = N.Fn; void* Cd = N.ClientData; std::string IdCopy = Id;
				D->Post([Fn, Cd, User, IdCopy]
				{
					EOS_Achievements_OnAchievementsUnlockedCallbackV2Info Info = {};
					Info.ClientData = Cd; Info.UserId = User; Info.AchievementId = IdCopy.c_str(); Info.UnlockTime = kUnlockTime;
					Fn(&Info);
				});
			}
		}
	}

	if (CompletionDelegate == nullptr || D == nullptr) return;
	D->Post([CompletionDelegate, ClientData, Result, User, Count]
	{
		EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo Info = {};
		Info.ResultCode = Result; Info.ClientData = ClientData; Info.UserId = User; Info.AchievementsCount = Count;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Achievements_AddNotifyAchievementsUnlockedV2(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallbackV2 NotificationFn)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (NotificationFn == nullptr) return EOS_INVALID_NOTIFICATIONID;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	const EOS_NotificationId Id = NextNotificationId();
	g_NotifyV2.push_back({Id, NotificationFn, ClientData});
	return Id;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Achievements_AddNotifyAchievementsUnlocked(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (NotificationFn == nullptr) return EOS_INVALID_NOTIFICATIONID;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	const EOS_NotificationId Id = NextNotificationId();
	g_NotifyV1.push_back({Id, NotificationFn, ClientData});
	return Id;
}

EOS_DECLARE_FUNC(void) EOS_Achievements_RemoveNotifyAchievementsUnlocked(EOS_HAchievements Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	for (auto It = g_NotifyV2.begin(); It != g_NotifyV2.end(); ++It) if (It->Id == InId) { g_NotifyV2.erase(It); return; }
	for (auto It = g_NotifyV1.begin(); It != g_NotifyV1.end(); ++It) if (It->Id == InId) { g_NotifyV1.erase(It); return; }
}

// Deprecated definition accessors -- empty catalog.
EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyAchievementDefinitionByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyAchievementDefinitionByIndexOptions* Options, EOS_Achievements_Definition** OutDefinition)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutDefinition) *OutDefinition = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyAchievementDefinitionByAchievementId(EOS_HAchievements Handle, const EOS_Achievements_CopyAchievementDefinitionByAchievementIdOptions* Options, EOS_Achievements_Definition** OutDefinition)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutDefinition) *OutDefinition = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Achievements_GetUnlockedAchievementCount(EOS_HAchievements Handle, const EOS_Achievements_GetUnlockedAchievementCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || Options->UserId == nullptr) return 0;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto* V = Find(Ids::ProductString(Options->UserId));
	return V ? static_cast<uint32_t>(V->size()) : 0;
}

namespace
{
	EOS_Achievements_UnlockedAchievement* MakeUnlocked(const std::string& Id)
	{
		EOS_Achievements_UnlockedAchievement* A = AllocApi<EOS_Achievements_UnlockedAchievement>();
		A->ApiVersion = EOS_ACHIEVEMENTS_UNLOCKEDACHIEVEMENT_API_LATEST;
		A->AchievementId = AttachString(A, Id.c_str());
		A->UnlockTime = kUnlockTime;
		return A;
	}
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyUnlockedAchievementByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyUnlockedAchievementByIndexOptions* Options, EOS_Achievements_UnlockedAchievement** OutAchievement)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutAchievement == nullptr || Options->UserId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutAchievement = nullptr;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto* V = Find(Ids::ProductString(Options->UserId));
	if (!V || Options->AchievementIndex >= V->size()) return EOS_EResult::EOS_NotFound;
	*OutAchievement = MakeUnlocked((*V)[Options->AchievementIndex]);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyUnlockedAchievementByAchievementId(EOS_HAchievements Handle, const EOS_Achievements_CopyUnlockedAchievementByAchievementIdOptions* Options, EOS_Achievements_UnlockedAchievement** OutAchievement)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutAchievement == nullptr || Options->UserId == nullptr || Options->AchievementId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutAchievement = nullptr;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto* V = Find(Ids::ProductString(Options->UserId));
	if (!V) return EOS_EResult::EOS_NotFound;
	for (const auto& Id : *V) if (Id == Options->AchievementId) { *OutAchievement = MakeUnlocked(Id); return EOS_EResult::EOS_Success; }
	return EOS_EResult::EOS_NotFound;
}

// Copy* releases.
EOS_DECLARE_FUNC(void) EOS_Achievements_DefinitionV2_Release(EOS_Achievements_DefinitionV2* AchievementDefinition) {
	EOSEMU_API_TRACE(); FreeApiBlock(AchievementDefinition); }
EOS_DECLARE_FUNC(void) EOS_Achievements_PlayerAchievement_Release(EOS_Achievements_PlayerAchievement* Achievement) {
	EOSEMU_API_TRACE(); FreeApiBlock(Achievement); }
EOS_DECLARE_FUNC(void) EOS_Achievements_Definition_Release(EOS_Achievements_Definition* AchievementDefinition) {
	EOSEMU_API_TRACE(); FreeApiBlock(AchievementDefinition); }
EOS_DECLARE_FUNC(void) EOS_Achievements_UnlockedAchievement_Release(EOS_Achievements_UnlockedAchievement* Achievement) {
	EOSEMU_API_TRACE(); FreeApiBlock(Achievement); }
