//
// Sanctions interface -- an active sanction on the local user, driven by config.
//
// There is no moderation backend on the LAN, so by default a user has no
// sanctions. When [Sanctions] Sanctioned=true, EOSEmu reports one active
// sanction against the local user -- enough to exercise a game's ban-handling
// path locally. We only ever answer for our own Product User ID; a remote
// player's sanction state is unknowable, so those queries return zero.
//
// Follows the Stats/Achievements pattern: the handle is a platform sentinel and
// is ignored; state comes from CurrentPlatform()->Config().
//

#include "interfaces/Interfaces.h"
#include "core/Logging.h"

#include "core/Config.h"
#include "core/Identity.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_sanctions_types.h"

#include <string>

namespace EOSEmu
{
	namespace
	{
		Dispatcher* Dispatch()
		{
			Platform* P = CurrentPlatform();
			return P ? &P->Dispatch() : nullptr;
		}

		// True if the target is the local user and a sanction is configured.
		bool IsSanctionedTarget(EOS_ProductUserId Target)
		{
			Platform* P = CurrentPlatform();
			if (P == nullptr || Target == nullptr) return false;
			if (!P->Config().GetBool("Sanctions", "Sanctioned", false)) return false;
			return Target == P->LocalIdentity().ProductUserId();
		}

		EOS_Sanctions_PlayerSanction* MakeSanction()
		{
			Platform* P = CurrentPlatform();
			const std::string Action = P ? P->Config().GetString("Sanctions", "Action", "RESTRICT_GAME_ACCESS") : "RESTRICT_GAME_ACCESS";
			const std::string RefId = P ? P->Config().GetString("Sanctions", "ReferenceId", "eosemu-sanction-0001") : "eosemu-sanction-0001";
			const int64_t TimePlaced = P ? static_cast<int64_t>(P->Config().GetInt("Sanctions", "TimePlaced", 1700000000)) : 1700000000;
			const int64_t TimeExpires = P ? static_cast<int64_t>(P->Config().GetInt("Sanctions", "TimeExpires", 0)) : 0;

			EOS_Sanctions_PlayerSanction* S = AllocApi<EOS_Sanctions_PlayerSanction>();
			S->ApiVersion = EOS_SANCTIONS_PLAYERSANCTION_API_LATEST;
			S->TimePlaced = TimePlaced;
			S->Action = AttachString(S, Action.c_str());
			S->TimeExpires = TimeExpires; // 0 == permanent, per the header
			S->ReferenceId = AttachString(S, RefId.c_str());
			return S;
		}
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Sanctions_QueryActivePlayerSanctions(EOS_HSanctions Handle, const EOS_Sanctions_QueryActivePlayerSanctionsOptions* Options, void* ClientData, const EOS_Sanctions_OnQueryActivePlayerSanctionsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_ProductUserId Target = Options ? Options->TargetUserId : nullptr;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	D->Post([CompletionDelegate, ClientData, Target, Local]
	{
		EOS_Sanctions_QueryActivePlayerSanctionsCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.TargetUserId = Target;
		Info.LocalUserId = Local;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Sanctions_GetPlayerSanctionCount(EOS_HSanctions Handle, const EOS_Sanctions_GetPlayerSanctionCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr) return 0;
	return IsSanctionedTarget(Options->TargetUserId) ? 1u : 0u;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sanctions_CopyPlayerSanctionByIndex(EOS_HSanctions Handle, const EOS_Sanctions_CopyPlayerSanctionByIndexOptions* Options, EOS_Sanctions_PlayerSanction** OutSanction)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutSanction == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutSanction = nullptr;
	if (!IsSanctionedTarget(Options->TargetUserId) || Options->SanctionIndex != 0) return EOS_EResult::EOS_NotFound;
	*OutSanction = MakeSanction();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Sanctions_CreatePlayerSanctionAppeal(EOS_HSanctions Handle, const EOS_Sanctions_CreatePlayerSanctionAppealOptions* Options, void* ClientData, const EOS_Sanctions_CreatePlayerSanctionAppealCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionDelegate == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	std::string RefId = (Options && Options->ReferenceId) ? Options->ReferenceId : "";
	D->Post([CompletionDelegate, ClientData, RefId]
	{
		EOS_Sanctions_CreatePlayerSanctionAppealCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.ReferenceId = RefId.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Sanctions_PlayerSanction_Release(EOS_Sanctions_PlayerSanction* Sanction)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(Sanction);
}
