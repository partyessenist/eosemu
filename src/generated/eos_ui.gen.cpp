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

#include "eos_ui.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_ConfigureOnScreenKeyboard(EOS_HUI Handle, const EOS_UI_ConfigureOnScreenKeyboardOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyOnScreenKeyboardRequested(EOS_HUI Handle, const EOS_UI_AddNotifyOnScreenKeyboardRequestedOptions* Options, void* ClientData, const EOS_UI_OnScreenKeyboardRequestedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyOnScreenKeyboardRequested(EOS_HUI Handle, EOS_NotificationId Id)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}
