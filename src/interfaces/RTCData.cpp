//
// RTCData -- data-channel surface of the RTC shim. Participants are reported
// with the data channel Enabled and SendData accepts (and drops) packets; no
// payload is relayed between peers. Making this a real channel over the LAN
// transport is a possible follow-up (HANDOFF.md, "P1 -- RTC", optional
// stretch). See RTC.h for the shim design.
//

#include "interfaces/RTC.h"
#include "core/Logging.h"

#include "core/Ids.h"
#include "core/Platform.h"

using namespace EOSEmu;

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCData_AddNotifyDataReceived(EOS_HRTCData Handle, const EOS_RTCData_AddNotifyDataReceivedOptions* Options, void* ClientData, const EOS_RTCData_OnDataReceivedCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCDataInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	// Registered for real; never fires because no data is relayed.
	return I->DataReceived_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCData_RemoveNotifyDataReceived(EOS_HRTCData Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCDataInterface>(Handle)) I->DataReceived_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCData_SendData(EOS_HRTCData Handle, const EOS_RTCData_SendDataOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCDataInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr
		|| Options->Data == nullptr || Options->DataLengthBytes == 0
		|| Options->DataLengthBytes > EOS_RTCDATA_MAX_PACKET_SIZE)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	if (!VersionInRange(Options->ApiVersion, EOS_RTCDATA_SENDDATA_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	if (!I->RTC().IsInRoom(Options->RoomName)) return EOS_EResult::EOS_NotFound;
	// "Queued for sending" -- and then dropped; no relay exists.
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_RTCData_UpdateSending(EOS_HRTCData Handle, const EOS_RTCData_UpdateSendingOptions* Options, void* ClientData, const EOS_RTCData_OnUpdateSendingCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCDataInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_Bool Enabled = Options ? Options->bDataEnabled : EOS_FALSE;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCDATA_UPDATESENDING_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().IsInRoom(RoomName))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Enabled, RoomName]
	{
		EOS_RTCData_UpdateSendingCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.bDataEnabled = Enabled;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTCData_UpdateReceiving(EOS_HRTCData Handle, const EOS_RTCData_UpdateReceivingOptions* Options, void* ClientData, const EOS_RTCData_OnUpdateReceivingCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCDataInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Participant = Options ? Options->ParticipantId : nullptr;
	EOS_Bool Enabled = Options ? Options->bDataEnabled : EOS_FALSE;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTCDATA_UPDATERECEIVING_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RTC().IsInRoom(RoomName)
		|| (Participant != nullptr && !I->RTC().RoomHasParticipant(RoomName, Ids::ProductString(Participant))))
	{
		Result = EOS_EResult::EOS_NotFound;
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Participant, Enabled, RoomName]
	{
		EOS_RTCData_UpdateReceivingCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.ParticipantId = Participant;
		Info.bDataEnabled = Enabled;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCData_AddNotifyParticipantUpdated(EOS_HRTCData Handle, const EOS_RTCData_AddNotifyParticipantUpdatedOptions* Options, void* ClientData, const EOS_RTCData_OnParticipantUpdatedCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCDataInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->ParticipantUpdated_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTCData_RemoveNotifyParticipantUpdated(EOS_HRTCData Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCDataInterface>(Handle)) I->ParticipantUpdated_.Remove(NotificationId);
}
