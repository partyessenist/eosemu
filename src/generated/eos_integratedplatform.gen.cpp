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

#include "eos_integratedplatform.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatformOptionsContainer_Add(EOS_HIntegratedPlatformOptionsContainer Handle, const EOS_IntegratedPlatformOptionsContainer_AddOptions* InOptions)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserLoginStatus(EOS_HIntegratedPlatform Handle, const EOS_IntegratedPlatform_SetUserLoginStatusOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_IntegratedPlatform_AddNotifyUserLoginStatusChanged(EOS_HIntegratedPlatform Handle, const EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions* Options, void* ClientData, const EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback CallbackFunction)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_RemoveNotifyUserLoginStatusChanged(EOS_HIntegratedPlatform Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserPreLogoutCallback(EOS_HIntegratedPlatform Handle, const EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions* Options, void* ClientData, EOS_IntegratedPlatform_OnUserPreLogoutCallback CallbackFunction)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_ClearUserPreLogoutCallback(EOS_HIntegratedPlatform Handle, const EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_FinalizeDeferredUserLogout(EOS_HIntegratedPlatform Handle, const EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}
