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

#include "eos_kws.h"

EOS_DECLARE_FUNC(void) EOS_KWS_QueryAgeGate(EOS_HKWS Handle, const EOS_KWS_QueryAgeGateOptions* Options, void* ClientData, const EOS_KWS_OnQueryAgeGateCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(void) EOS_KWS_CreateUser(EOS_HKWS Handle, const EOS_KWS_CreateUserOptions* Options, void* ClientData, const EOS_KWS_OnCreateUserCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(void) EOS_KWS_QueryPermissions(EOS_HKWS Handle, const EOS_KWS_QueryPermissionsOptions* Options, void* ClientData, const EOS_KWS_OnQueryPermissionsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(void) EOS_KWS_UpdateParentEmail(EOS_HKWS Handle, const EOS_KWS_UpdateParentEmailOptions* Options, void* ClientData, const EOS_KWS_OnUpdateParentEmailCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(void) EOS_KWS_RequestPermissions(EOS_HKWS Handle, const EOS_KWS_RequestPermissionsOptions* Options, void* ClientData, const EOS_KWS_OnRequestPermissionsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(int32_t) EOS_KWS_GetPermissionsCount(EOS_HKWS Handle, const EOS_KWS_GetPermissionsCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<int32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_KWS_CopyPermissionByIndex(EOS_HKWS Handle, const EOS_KWS_CopyPermissionByIndexOptions* Options, EOS_KWS_PermissionStatus ** OutPermission)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_KWS_GetPermissionByKey(EOS_HKWS Handle, const EOS_KWS_GetPermissionByKeyOptions* Options, EOS_EKWSPermissionStatus* OutPermission)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_KWS_AddNotifyPermissionsUpdateReceived(EOS_HKWS Handle, const EOS_KWS_AddNotifyPermissionsUpdateReceivedOptions* Options, void* ClientData, const EOS_KWS_OnPermissionsUpdateReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_KWS_RemoveNotifyPermissionsUpdateReceived(EOS_HKWS Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}
