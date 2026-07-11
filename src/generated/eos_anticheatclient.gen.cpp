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

#include "eos_anticheatclient.h"

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyMessageToServer(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_AddNotifyMessageToServerOptions* Options, void* ClientData, EOS_AntiCheatClient_OnMessageToServerCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyMessageToServer(EOS_HAntiCheatClient Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyMessageToPeer(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_AddNotifyMessageToPeerOptions* Options, void* ClientData, EOS_AntiCheatClient_OnMessageToPeerCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyMessageToPeer(EOS_HAntiCheatClient Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyPeerActionRequired(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_AddNotifyPeerActionRequiredOptions* Options, void* ClientData, EOS_AntiCheatClient_OnPeerActionRequiredCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyPeerActionRequired(EOS_HAntiCheatClient Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyPeerAuthStatusChanged(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_AddNotifyPeerAuthStatusChangedOptions* Options, void* ClientData, EOS_AntiCheatClient_OnPeerAuthStatusChangedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyPeerAuthStatusChanged(EOS_HAntiCheatClient Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyClientIntegrityViolated(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_AddNotifyClientIntegrityViolatedOptions* Options, void* ClientData, EOS_AntiCheatClient_OnClientIntegrityViolatedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyClientIntegrityViolated(EOS_HAntiCheatClient Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_BeginSession(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_BeginSessionOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_EndSession(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_EndSessionOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_PollStatus(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_PollStatusOptions* Options, EOS_EAntiCheatClientViolationType* OutViolationType, char* OutMessage)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_Reserved01(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_Reserved01Options* Options, int32_t* OutValue)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_Reserved02(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_Reserved02Options* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_GetModuleBuildId(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_GetModuleBuildIdOptions* Options, uint32_t* OutModuleBuildId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_AddExternalIntegrityCatalog(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_AddExternalIntegrityCatalogOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_ReceiveMessageFromServer(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_ReceiveMessageFromServerOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_GetProtectMessageOutputLength(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_GetProtectMessageOutputLengthOptions* Options, uint32_t* OutBufferSizeBytes)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_ProtectMessage(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_ProtectMessageOptions* Options, void* OutBuffer, uint32_t* OutBytesWritten)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_UnprotectMessage(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_UnprotectMessageOptions* Options, void* OutBuffer, uint32_t* OutBytesWritten)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_RegisterPeer(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_RegisterPeerOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_UnregisterPeer(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_UnregisterPeerOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_ReceiveMessageFromPeer(EOS_HAntiCheatClient Handle, const EOS_AntiCheatClient_ReceiveMessageFromPeerOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}
