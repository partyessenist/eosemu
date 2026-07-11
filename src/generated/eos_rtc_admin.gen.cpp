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

#include "eos_rtc_admin.h"

EOS_DECLARE_FUNC(void) EOS_RTCAdmin_QueryJoinRoomToken(EOS_HRTCAdmin Handle, const EOS_RTCAdmin_QueryJoinRoomTokenOptions* Options, void* ClientData, const EOS_RTCAdmin_OnQueryJoinRoomTokenCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAdmin_CopyUserTokenByIndex(EOS_HRTCAdmin Handle, const EOS_RTCAdmin_CopyUserTokenByIndexOptions* Options, EOS_RTCAdmin_UserToken ** OutUserToken)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAdmin_CopyUserTokenByUserId(EOS_HRTCAdmin Handle, const EOS_RTCAdmin_CopyUserTokenByUserIdOptions* Options, EOS_RTCAdmin_UserToken ** OutUserToken)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(void) EOS_RTCAdmin_Kick(EOS_HRTCAdmin Handle, const EOS_RTCAdmin_KickOptions* Options, void* ClientData, const EOS_RTCAdmin_OnKickCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(void) EOS_RTCAdmin_SetParticipantHardMute(EOS_HRTCAdmin Handle, const EOS_RTCAdmin_SetParticipantHardMuteOptions* Options, void* ClientData, const EOS_RTCAdmin_OnSetParticipantHardMuteCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}
