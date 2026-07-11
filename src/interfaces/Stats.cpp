//
// Stats interface -- real in-memory ingest/query with read-back.
//
// Stats have observable read-back (ingest then query then copy), so this stores
// them per target user in a process-global table rather than reporting empty
// success. The handle is a platform sentinel and is ignored; dispatch routes
// through the live platform. See CLAUDE.md, "Real ingest/query semantics".
//

#include "interfaces/Interfaces.h"
#include "core/Logging.h"
#include "interfaces/LocalStores.h"

#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_stats_types.h"

#include <map>
#include <mutex>
#include <string>

namespace EOSEmu
{
	namespace
	{
		std::mutex g_StatsMutex;
		// productId -> (statName -> value)
		std::map<std::string, std::map<std::string, int32_t>> g_Stats;

		Dispatcher* Dispatch()
		{
			Platform* P = CurrentPlatform();
			return P ? &P->Dispatch() : nullptr;
		}
	}

	void SeedStat(const std::string& ProductId, const std::string& Name, int32_t Value)
	{
		if (ProductId.empty() || Name.empty()) return;
		std::lock_guard<std::mutex> Lock(g_StatsMutex);
		g_Stats[ProductId][Name] = Value; // overwrite: seeding is idempotent
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Stats_IngestStat(EOS_HStats Handle, const EOS_Stats_IngestStatOptions* Options, void* ClientData, const EOS_Stats_OnIngestStatCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
	EOS_ProductUserId Local = nullptr, Target = nullptr;
	if (Options != nullptr)
	{
		Local = Options->LocalUserId;
		Target = Options->TargetUserId ? Options->TargetUserId : Options->LocalUserId;
		if (Target != nullptr)
		{
			const std::string Key = Ids::ProductString(Target);
			std::lock_guard<std::mutex> Lock(g_StatsMutex);
			auto& Table = g_Stats[Key];
			for (uint32_t i = 0; Options->Stats != nullptr && i < Options->StatsCount; ++i)
			{
				if (Options->Stats[i].StatName != nullptr)
					Table[Options->Stats[i].StatName] += Options->Stats[i].IngestAmount;
			}
			Result = EOS_EResult::EOS_Success;
		}
	}
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch();
	if (D == nullptr) return;
	D->Post([CompletionDelegate, ClientData, Result, Local, Target]
	{
		EOS_Stats_IngestStatCompleteCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Stats_QueryStats(EOS_HStats Handle, const EOS_Stats_QueryStatsOptions* Options, void* ClientData, const EOS_Stats_OnQueryStatsCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch();
	if (D == nullptr) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Target = Options ? (Options->TargetUserId ? Options->TargetUserId : Options->LocalUserId) : nullptr;
	D->Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_Stats_OnQueryStatsCompleteCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Stats_GetStatsCount(EOS_HStats Handle, const EOS_Stats_GetStatCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || Options->TargetUserId == nullptr) return 0;
	std::lock_guard<std::mutex> Lock(g_StatsMutex);
	auto It = g_Stats.find(Ids::ProductString(Options->TargetUserId));
	return It != g_Stats.end() ? static_cast<uint32_t>(It->second.size()) : 0;
}

namespace
{
	EOS_Stats_Stat* MakeStat(const std::string& Name, int32_t Value)
	{
		EOS_Stats_Stat* Stat = AllocApi<EOS_Stats_Stat>();
		Stat->ApiVersion = EOS_STATS_STAT_API_LATEST;
		Stat->Name = AttachString(Stat, Name.c_str());
		Stat->StartTime = EOS_STATS_TIME_UNDEFINED;
		Stat->EndTime = EOS_STATS_TIME_UNDEFINED;
		Stat->Value = Value;
		return Stat;
	}
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Stats_CopyStatByIndex(EOS_HStats Handle, const EOS_Stats_CopyStatByIndexOptions* Options, EOS_Stats_Stat** OutStat)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutStat == nullptr || Options->TargetUserId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutStat = nullptr;
	std::lock_guard<std::mutex> Lock(g_StatsMutex);
	auto It = g_Stats.find(Ids::ProductString(Options->TargetUserId));
	if (It == g_Stats.end() || Options->StatIndex >= It->second.size()) return EOS_EResult::EOS_NotFound;
	auto Sit = It->second.begin();
	std::advance(Sit, Options->StatIndex);
	*OutStat = MakeStat(Sit->first, Sit->second);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Stats_CopyStatByName(EOS_HStats Handle, const EOS_Stats_CopyStatByNameOptions* Options, EOS_Stats_Stat** OutStat)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutStat == nullptr || Options->TargetUserId == nullptr || Options->Name == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutStat = nullptr;
	std::lock_guard<std::mutex> Lock(g_StatsMutex);
	auto It = g_Stats.find(Ids::ProductString(Options->TargetUserId));
	if (It == g_Stats.end()) return EOS_EResult::EOS_NotFound;
	auto Sit = It->second.find(Options->Name);
	if (Sit == It->second.end()) return EOS_EResult::EOS_NotFound;
	*OutStat = MakeStat(Sit->first, Sit->second);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Stats_Stat_Release(EOS_Stats_Stat* Stat)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(Stat);
}
