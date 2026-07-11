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

#include "eos_rtc.h"

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTC_AddNotifyRoomBeforeJoin(EOS_HRTC Handle, const EOS_RTC_AddNotifyRoomBeforeJoinOptions* Options, void* ClientData, const EOS_RTC_OnRoomBeforeJoinCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_RTC_RemoveNotifyRoomBeforeJoin(EOS_HRTC Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
}
