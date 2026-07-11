//
// Leaderboards -- empty but successful catalog.
//
// Leaderboard definitions and ranks are configured server-side and computed by
// the backend; a LAN emulator has neither. Queries succeed with no results, so
// callers see a working-but-empty leaderboard rather than a hard error. Copy*
// accessors report NotFound; counts are zero.
//

#include "interfaces/Interfaces.h"
#include "core/Logging.h"

#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_leaderboards_types.h"

namespace EOSEmu
{
	namespace { Dispatcher* Dispatch() { Platform* P = CurrentPlatform(); return P ? &P->Dispatch() : nullptr; } }
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Leaderboards_QueryLeaderboardDefinitions(EOS_HLeaderboards Handle, const EOS_Leaderboards_QueryLeaderboardDefinitionsOptions* Options, void* ClientData, const EOS_Leaderboards_OnQueryLeaderboardDefinitionsCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	D->Post([CompletionDelegate, ClientData]
	{
		EOS_Leaderboards_OnQueryLeaderboardDefinitionsCompleteCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Leaderboards_GetLeaderboardDefinitionCount(EOS_HLeaderboards Handle, const EOS_Leaderboards_GetLeaderboardDefinitionCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Leaderboards_CopyLeaderboardDefinitionByIndex(EOS_HLeaderboards Handle, const EOS_Leaderboards_CopyLeaderboardDefinitionByIndexOptions* Options, EOS_Leaderboards_Definition** OutLeaderboardDefinition)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutLeaderboardDefinition) *OutLeaderboardDefinition = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Leaderboards_CopyLeaderboardDefinitionByLeaderboardId(EOS_HLeaderboards Handle, const EOS_Leaderboards_CopyLeaderboardDefinitionByLeaderboardIdOptions* Options, EOS_Leaderboards_Definition** OutLeaderboardDefinition)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutLeaderboardDefinition) *OutLeaderboardDefinition = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Leaderboards_QueryLeaderboardRanks(EOS_HLeaderboards Handle, const EOS_Leaderboards_QueryLeaderboardRanksOptions* Options, void* ClientData, const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	std::string Id = (Options && Options->LeaderboardId) ? Options->LeaderboardId : "";
	D->Post([CompletionDelegate, ClientData, Id]
	{
		EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData; Info.LeaderboardId = Id.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Leaderboards_GetLeaderboardRecordCount(EOS_HLeaderboards Handle, const EOS_Leaderboards_GetLeaderboardRecordCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Leaderboards_CopyLeaderboardRecordByIndex(EOS_HLeaderboards Handle, const EOS_Leaderboards_CopyLeaderboardRecordByIndexOptions* Options, EOS_Leaderboards_LeaderboardRecord** OutLeaderboardRecord)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutLeaderboardRecord) *OutLeaderboardRecord = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Leaderboards_CopyLeaderboardRecordByUserId(EOS_HLeaderboards Handle, const EOS_Leaderboards_CopyLeaderboardRecordByUserIdOptions* Options, EOS_Leaderboards_LeaderboardRecord** OutLeaderboardRecord)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutLeaderboardRecord) *OutLeaderboardRecord = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Leaderboards_QueryLeaderboardUserScores(EOS_HLeaderboards Handle, const EOS_Leaderboards_QueryLeaderboardUserScoresOptions* Options, void* ClientData, const EOS_Leaderboards_OnQueryLeaderboardUserScoresCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	D->Post([CompletionDelegate, ClientData]
	{
		EOS_Leaderboards_OnQueryLeaderboardUserScoresCompleteCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Leaderboards_GetLeaderboardUserScoreCount(EOS_HLeaderboards Handle, const EOS_Leaderboards_GetLeaderboardUserScoreCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Leaderboards_CopyLeaderboardUserScoreByIndex(EOS_HLeaderboards Handle, const EOS_Leaderboards_CopyLeaderboardUserScoreByIndexOptions* Options, EOS_Leaderboards_LeaderboardUserScore** OutLeaderboardUserScore)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutLeaderboardUserScore) *OutLeaderboardUserScore = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Leaderboards_CopyLeaderboardUserScoreByUserId(EOS_HLeaderboards Handle, const EOS_Leaderboards_CopyLeaderboardUserScoreByUserIdOptions* Options, EOS_Leaderboards_LeaderboardUserScore** OutLeaderboardUserScore)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutLeaderboardUserScore) *OutLeaderboardUserScore = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Leaderboards_Definition_Release(EOS_Leaderboards_Definition* LeaderboardDefinition) {
	EOSEMU_API_TRACE(); EOSEmu::FreeApiBlock(LeaderboardDefinition); }
EOS_DECLARE_FUNC(void) EOS_Leaderboards_LeaderboardDefinition_Release(EOS_Leaderboards_Definition* LeaderboardDefinition) {
	EOSEMU_API_TRACE(); EOSEmu::FreeApiBlock(LeaderboardDefinition); }
EOS_DECLARE_FUNC(void) EOS_Leaderboards_LeaderboardRecord_Release(EOS_Leaderboards_LeaderboardRecord* LeaderboardRecord) {
	EOSEMU_API_TRACE(); EOSEmu::FreeApiBlock(LeaderboardRecord); }
EOS_DECLARE_FUNC(void) EOS_Leaderboards_LeaderboardUserScore_Release(EOS_Leaderboards_LeaderboardUserScore* LeaderboardUserScore) {
	EOSEMU_API_TRACE(); EOSEmu::FreeApiBlock(LeaderboardUserScore); }
