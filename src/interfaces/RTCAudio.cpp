//
// RTCAudio -- audio control surface of the RTC shim. No frames ever flow;
// see RTC.h for the design. Every completion posts via the dispatcher (the
// Tick-thread rule) and echoes the request's fields as the header documents.
//
// One fake input and one fake output device are reported so a game's audio
// settings screen has something to show. The deprecated device/settings
// family (EOS_RTCAudio_GetAudioInputDevicesCount et al.) stays a generated
// stub reporting zero devices.
//

#include "interfaces/RTC.h"
#include "core/Logging.h"

#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Platform.h"

namespace EOSEmu
{
	namespace
	{
		constexpr const char* kInputDeviceId = "eosemu_default_input";
		constexpr const char* kInputDeviceName = "EOSEmu Default Input";
		constexpr const char* kOutputDeviceId = "eosemu_default_output";
		constexpr const char* kOutputDeviceName = "EOSEmu Default Output";
	}
}

using namespace EOSEmu;

// ----------------------------------------------------------------------------
// Sending / receiving / volume. All accepted; state kept only for the local
// user's sending status so a mute toggle round-trips.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SendAudio(EOS_HRTCAudio Handle, const EOS_RTCAudio_SendAudioOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr || Options->Buffer == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_SENDAUDIO_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	if (!I->RTC().IsInRoom(Options->RoomName)) return EOS_EResult::EOS_NotFound;
	// Accepted and dropped; there is no peer audio pipeline.
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateSending(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateSendingOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateSendingCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ERTCAudioStatus Status = Options ? Options->AudioStatus : EOS_ERTCAudioStatus::EOS_RTCAS_Disabled;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_UPDATESENDING_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().SetLocalAudioStatus(RoomName, Status))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Status, RoomName]
	{
		EOS_RTCAudio_UpdateSendingCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.AudioStatus = Status;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateReceiving(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateReceivingOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateReceivingCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Participant = Options ? Options->ParticipantId : nullptr;
	EOS_Bool Enabled = Options ? Options->bAudioEnabled : EOS_FALSE;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_UPDATERECEIVING_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().IsInRoom(RoomName)
		|| (Participant != nullptr && !I->RTC().RoomHasParticipant(RoomName, Ids::ProductString(Participant))))
	{
		// Documented: NotFound when the participant isn't in the room.
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Participant, Enabled, RoomName]
	{
		EOS_RTCAudio_UpdateReceivingCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.ParticipantId = Participant;
		Info.bAudioEnabled = Enabled;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateSendingVolume(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateSendingVolumeOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateSendingVolumeCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	float Volume = Options ? Options->Volume : 0.0f;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_UPDATESENDINGVOLUME_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().IsInRoom(RoomName))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Volume, RoomName]
	{
		EOS_RTCAudio_UpdateSendingVolumeCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.Volume = Volume;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateReceivingVolume(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateReceivingVolumeOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateReceivingVolumeCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	float Volume = Options ? Options->Volume : 0.0f;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_UPDATERECEIVINGVOLUME_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().IsInRoom(RoomName))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Volume, RoomName]
	{
		EOS_RTCAudio_UpdateReceivingVolumeCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.Volume = Volume;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateParticipantVolume(EOS_HRTCAudio Handle, const EOS_RTCAudio_UpdateParticipantVolumeOptions* Options, void* ClientData, const EOS_RTCAudio_OnUpdateParticipantVolumeCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Participant = Options ? Options->ParticipantId : nullptr;
	float Volume = Options ? Options->Volume : 0.0f;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_UPDATEPARTICIPANTVOLUME_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().IsInRoom(RoomName)
		|| (Participant != nullptr && !I->RTC().RoomHasParticipant(RoomName, Ids::ProductString(Participant))))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Participant, Volume, RoomName]
	{
		EOS_RTCAudio_UpdateParticipantVolumeCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.ParticipantId = Participant;
		Info.Volume = Volume;
		CompletionDelegate(&Info);
	});
}

// ----------------------------------------------------------------------------
// Notifications. Participant updates fire from the RTC pump; the device and
// state notifications are registered for real but never fire (nothing changes).
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyParticipantUpdated(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyParticipantUpdatedOptions* Options, void* ClientData, const EOS_RTCAudio_OnParticipantUpdatedCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->ParticipantUpdated_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyParticipantUpdated(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCAudioInterface>(Handle)) I->ParticipantUpdated_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioDevicesChanged(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioDevicesChangedOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioDevicesChangedCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<RTCAudioInterface>(Handle);
	return I ? I->DevicesChanged_.Add(CompletionDelegate, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioDevicesChanged(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCAudioInterface>(Handle)) I->DevicesChanged_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioInputState(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioInputStateOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioInputStateCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->InputState_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioInputState(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCAudioInterface>(Handle)) I->InputState_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioOutputState(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioOutputStateOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioOutputStateCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->OutputState_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioOutputState(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCAudioInterface>(Handle)) I->OutputState_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioBeforeSend(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioBeforeSendOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioBeforeSendCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->BeforeSend_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioBeforeSend(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCAudioInterface>(Handle)) I->BeforeSend_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCAudio_AddNotifyAudioBeforeRender(EOS_HRTCAudio Handle, const EOS_RTCAudio_AddNotifyAudioBeforeRenderOptions* Options, void* ClientData, const EOS_RTCAudio_OnAudioBeforeRenderCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->BeforeRender_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RemoveNotifyAudioBeforeRender(EOS_HRTCAudio Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCAudioInterface>(Handle)) I->BeforeRender_.Remove(NotificationId);
}

// ----------------------------------------------------------------------------
// Platform users and device settings -- accepted, echoed, ignored.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RegisterPlatformUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_RegisterPlatformUserOptions* Options, void* ClientData, const EOS_RTCAudio_OnRegisterPlatformUserCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EResult Result = (Options == nullptr || Options->PlatformUserId == nullptr)
		? EOS_EResult::EOS_InvalidParameters : EOS_EResult::EOS_Success;
	std::string User = (Options && Options->PlatformUserId) ? Options->PlatformUserId : "";
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, User]
	{
		EOS_RTCAudio_OnRegisterPlatformUserCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.PlatformUserId = User.empty() ? nullptr : User.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UnregisterPlatformUser(EOS_HRTCAudio Handle, const EOS_RTCAudio_UnregisterPlatformUserOptions* Options, void* ClientData, const EOS_RTCAudio_OnUnregisterPlatformUserCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EResult Result = (Options == nullptr || Options->PlatformUserId == nullptr)
		? EOS_EResult::EOS_InvalidParameters : EOS_EResult::EOS_Success;
	std::string User = (Options && Options->PlatformUserId) ? Options->PlatformUserId : "";
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, User]
	{
		EOS_RTCAudio_OnUnregisterPlatformUserCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.PlatformUserId = User.empty() ? nullptr : User.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_SetInputDeviceSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetInputDeviceSettingsOptions* Options, void* ClientData, const EOS_RTCAudio_OnSetInputDeviceSettingsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EResult Result = (Options == nullptr || Options->LocalUserId == nullptr)
		? EOS_EResult::EOS_InvalidParameters : EOS_EResult::EOS_Success;
	std::string Device = (Options && Options->RealDeviceId) ? Options->RealDeviceId : "";
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Device]
	{
		EOS_RTCAudio_OnSetInputDeviceSettingsCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.RealDeviceId = Device.empty() ? nullptr : Device.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_SetOutputDeviceSettings(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetOutputDeviceSettingsOptions* Options, void* ClientData, const EOS_RTCAudio_OnSetOutputDeviceSettingsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EResult Result = (Options == nullptr || Options->LocalUserId == nullptr)
		? EOS_EResult::EOS_InvalidParameters : EOS_EResult::EOS_Success;
	std::string Device = (Options && Options->RealDeviceId) ? Options->RealDeviceId : "";
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Device]
	{
		EOS_RTCAudio_OnSetOutputDeviceSettingsCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.RealDeviceId = Device.empty() ? nullptr : Device.c_str();
		CompletionDelegate(&Info);
	});
}

// ----------------------------------------------------------------------------
// Device enumeration -- one fake input and one fake output device.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_RTCAudio_QueryInputDevicesInformation(EOS_HRTCAudio Handle, const EOS_RTCAudio_QueryInputDevicesInformationOptions* Options, void* ClientData, const EOS_RTCAudio_OnQueryInputDevicesInformationCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_RTCAudio_OnQueryInputDevicesInformationCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetInputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetInputDevicesCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	return As<RTCAudioInterface>(Handle) ? 1u : 0u;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_CopyInputDeviceInformationByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_CopyInputDeviceInformationByIndexOptions* Options, EOS_RTCAudio_InputDeviceInformation** OutInputDeviceInformation)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutInputDeviceInformation == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutInputDeviceInformation = nullptr;
	if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_COPYINPUTDEVICEINFORMATIONBYINDEX_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	if (Options->DeviceIndex != 0) return EOS_EResult::EOS_NotFound;
	EOS_RTCAudio_InputDeviceInformation* Out = AllocApi<EOS_RTCAudio_InputDeviceInformation>();
	Out->ApiVersion = EOS_RTCAUDIO_INPUTDEVICEINFORMATION_API_LATEST;
	Out->bDefaultDevice = EOS_TRUE;
	Out->DeviceId = AttachString(Out, kInputDeviceId);
	Out->DeviceName = AttachString(Out, kInputDeviceName);
	*OutInputDeviceInformation = Out;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_InputDeviceInformation_Release(EOS_RTCAudio_InputDeviceInformation* DeviceInformation)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(DeviceInformation);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_QueryOutputDevicesInformation(EOS_HRTCAudio Handle, const EOS_RTCAudio_QueryOutputDevicesInformationOptions* Options, void* ClientData, const EOS_RTCAudio_OnQueryOutputDevicesInformationCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_RTCAudio_OnQueryOutputDevicesInformationCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_RTCAudio_GetOutputDevicesCount(EOS_HRTCAudio Handle, const EOS_RTCAudio_GetOutputDevicesCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	return As<RTCAudioInterface>(Handle) ? 1u : 0u;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_CopyOutputDeviceInformationByIndex(EOS_HRTCAudio Handle, const EOS_RTCAudio_CopyOutputDeviceInformationByIndexOptions* Options, EOS_RTCAudio_OutputDeviceInformation** OutOutputDeviceInformation)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCAudioInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutOutputDeviceInformation == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutOutputDeviceInformation = nullptr;
	if (!VersionInRange(Options->ApiVersion, EOS_RTCAUDIO_COPYOUTPUTDEVICEINFORMATIONBYINDEX_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	if (Options->DeviceIndex != 0) return EOS_EResult::EOS_NotFound;
	EOS_RTCAudio_OutputDeviceInformation* Out = AllocApi<EOS_RTCAudio_OutputDeviceInformation>();
	Out->ApiVersion = EOS_RTCAUDIO_OUTPUTDEVICEINFORMATION_API_LATEST;
	Out->bDefaultDevice = EOS_TRUE;
	Out->DeviceId = AttachString(Out, kOutputDeviceId);
	Out->DeviceName = AttachString(Out, kOutputDeviceName);
	*OutOutputDeviceInformation = Out;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_OutputDeviceInformation_Release(EOS_RTCAudio_OutputDeviceInformation* DeviceInformation)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(DeviceInformation);
}
