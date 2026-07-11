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

#include "eos_rtc_audio.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_RegisterPlatformAudioUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_RegisterPlatformAudioUserOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_UnregisterPlatformAudioUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_UnregisterPlatformAudioUserOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetAudioInputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioInputDevicesCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(const EOS_RTCAudio_AudioInputDeviceInfo *) EOS_RTCAudio_GetAudioInputDeviceByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioInputDeviceByIndexOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return nullptr;
}

EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetAudioOutputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioOutputDevicesCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(const EOS_RTCAudio_AudioOutputDeviceInfo *) EOS_RTCAudio_GetAudioOutputDeviceByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetAudioOutputDeviceByIndexOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return nullptr;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SetAudioInputSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetAudioInputSettingsOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SetAudioOutputSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetAudioOutputSettingsOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}
