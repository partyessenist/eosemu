// ============================================================================
//  GENERATED FILE -- DO NOT EDIT BY HAND.
//
//  Regenerate with:  python tools/gen_stubs.py
//
//  Every function the EOS SDK declares must be exported or the host process
//  fails to load.  These are placeholders: they trace the call and return an
//  inert value.  To implement one for real, add its name to
//  tools/hand_implemented.txt and define it in a hand-written source file.
// ============================================================================


#include "core/Stub.h"

#include "eos_anticheatserver.h"

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatServer_AddNotifyMessageToClient(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_AddNotifyMessageToClientOptions* Options, void* ClientData, EOS_AntiCheatServer_OnMessageToClientCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatServer_RemoveNotifyMessageToClient(EOS_HAntiCheatServer Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatServer_AddNotifyClientActionRequired(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_AddNotifyClientActionRequiredOptions* Options, void* ClientData, EOS_AntiCheatServer_OnClientActionRequiredCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatServer_RemoveNotifyClientActionRequired(EOS_HAntiCheatServer Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatServer_AddNotifyClientAuthStatusChanged(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_AddNotifyClientAuthStatusChangedOptions* Options, void* ClientData, EOS_AntiCheatServer_OnClientAuthStatusChangedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatServer_RemoveNotifyClientAuthStatusChanged(EOS_HAntiCheatServer Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_BeginSession(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_BeginSessionOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_EndSession(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_EndSessionOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_RegisterClient(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_RegisterClientOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_UnregisterClient(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_UnregisterClientOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_ReceiveMessageFromClient(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_ReceiveMessageFromClientOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_SetClientDetails(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_SetClientDetailsOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_SetGameSessionId(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_SetGameSessionIdOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_SetClientNetworkState(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_SetClientNetworkStateOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_GetProtectMessageOutputLength(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_GetProtectMessageOutputLengthOptions* Options, uint32_t* OutBufferSizeBytes)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_ProtectMessage(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_ProtectMessageOptions* Options, void* OutBuffer, uint32_t* OutBytesWritten)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_UnprotectMessage(EOS_HAntiCheatServer Handle, const EOS_AntiCheatServer_UnprotectMessageOptions* Options, void* OutBuffer, uint32_t* OutBytesWritten)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_RegisterEvent(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_RegisterEventOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogEvent(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogEventOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogGameRoundStart(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogGameRoundStartOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogGameRoundEnd(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogGameRoundEndOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerSpawn(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerSpawnOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerDespawn(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerDespawnOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerRevive(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerReviveOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerTick(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerTickOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerUseWeapon(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerUseWeaponOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerUseAbility(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerUseAbilityOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatServer_LogPlayerTakeDamage(EOS_HAntiCheatServer Handle, const EOS_AntiCheatCommon_LogPlayerTakeDamageOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}
