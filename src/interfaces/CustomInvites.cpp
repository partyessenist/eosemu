//
// Custom invites interface.
//
// SetCustomInvite stores the payload the local user will send; SendCustomInvite
// unicasts it to each target over the LAN, and the receiver's
// CustomInviteReceived notification fires. Request-to-join and the accept/reject
// flows report plausible successes; the native-invite path never fires.
//

#include "interfaces/CustomInvites.h"
#include "core/Logging.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Peers.h"
#include "core/Platform.h"
#include "net/Serialize.h"

#include <cstdio>

namespace EOSEmu
{
	EOS_EResult CustomInvitesInterface::SetCustomInvite(const char* Payload)
	{
		if (Payload == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		OutgoingPayload_ = Payload;
		return EOS_EResult::EOS_Success;
	}

	void CustomInvitesInterface::SendCustomInvite(EOS_ProductUserId Local, const EOS_ProductUserId* Targets, uint32_t Count, void* ClientData, EOS_CustomInvites_OnSendCustomInviteCallback Cb)
	{
		std::string Payload;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Payload = OutgoingPayload_;
		}
		const std::string LocalPid = Ids::ProductString(Local);
		std::vector<EOS_ProductUserId> Sent;
		for (uint32_t i = 0; Targets != nullptr && i < Count; ++i)
		{
			if (Targets[i] == nullptr) continue;
			Sent.push_back(Targets[i]);
			net::Endpoint To = Platform_.Peers().EndpointFor(Targets[i]);
			if (To.Valid())
			{
				net::ByteWriter W;
				W.Str(LocalPid);
				W.Str(Ids::ProductString(Targets[i]));
				W.Str(Payload);
				Platform_.Net().SendTo(To, net::MessageType::CustomInvite, W.Data().data(), W.Size());
			}
		}

		if (Cb == nullptr) return;
		Platform_.Dispatch().Post([Cb, ClientData, Local, Sent]() mutable
		{
			EOS_CustomInvites_SendCustomInviteCallbackInfo Info = {};
			Info.ResultCode = EOS_EResult::EOS_Success;
			Info.ClientData = ClientData;
			Info.LocalUserId = Local;
			Info.TargetUserIds = Sent.empty() ? nullptr : Sent.data();
			Info.TargetUserIdsCount = static_cast<uint32_t>(Sent.size());
			Cb(&Info);
		});
	}

	EOS_EResult CustomInvitesInterface::FinalizeInvite()
	{
		// Nothing to reconcile with a backend; the local processing result is
		// simply accepted.
		return EOS_EResult::EOS_Success;
	}

	void CustomInvitesInterface::OnDatagram(const net::Endpoint&, net::MessageType Type, const uint8_t* Payload, uint16_t Len)
	{
		if (Type != net::MessageType::CustomInvite) return;
		net::ByteReader R(Payload, Len);
		const std::string FromPid = R.Str();
		const std::string ToPid = R.Str();
		const std::string InvitePayload = R.Str();
		if (!R.Ok() || ToPid != Ids::ProductString(Platform_.LocalIdentity().ProductUserId())) return;

		std::string InviteId;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			char Buf[64]; std::snprintf(Buf, sizeof(Buf), "custominvite_%llu", static_cast<unsigned long long>(NextInvite_++));
			InviteId = Buf;
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		EOS_ProductUserId Sender = Ids::InternProduct(FromPid);

		for (const auto& E : Received_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			// Capture strings by value; the notification fires on the Tick thread.
			std::string IdCopy = InviteId, PayloadCopy = InvitePayload;
			Platform_.Dispatch().Post([Fn, Cd, Local, Sender, IdCopy, PayloadCopy]
			{
				EOS_CustomInvites_OnCustomInviteReceivedCallbackInfo Info = {};
				Info.ClientData = Cd;
				Info.LocalUserId = Local;
				Info.TargetUserId = Sender;
				Info.CustomInviteId = IdCopy.c_str();
				Info.Payload = PayloadCopy.c_str();
				Fn(&Info);
			});
		}
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(EOS_EResult) EOS_CustomInvites_SetCustomInvite(EOS_HCustomInvites Handle, const EOS_CustomInvites_SetCustomInviteOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<CustomInvitesInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->SetCustomInvite(Options->Payload);
}

EOS_DECLARE_FUNC(void) EOS_CustomInvites_SendCustomInvite(EOS_HCustomInvites Handle, const EOS_CustomInvites_SendCustomInviteOptions* Options, void* ClientData, const EOS_CustomInvites_OnSendCustomInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<CustomInvitesInterface>(Handle);
	if (I == nullptr || Options == nullptr) return;
	I->SendCustomInvite(Options->LocalUserId, Options->TargetUserIds, Options->TargetUserIdsCount, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyCustomInviteReceived(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyCustomInviteReceivedOptions* Options, void* ClientData, const EOS_CustomInvites_OnCustomInviteReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->Received_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyCustomInviteReceived(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->Received_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyCustomInviteAccepted(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyCustomInviteAcceptedOptions* Options, void* ClientData, const EOS_CustomInvites_OnCustomInviteAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->Accepted_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyCustomInviteAccepted(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->Accepted_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyCustomInviteRejected(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyCustomInviteRejectedOptions* Options, void* ClientData, const EOS_CustomInvites_OnCustomInviteRejectedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->Rejected_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyCustomInviteRejected(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->Rejected_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_EResult) EOS_CustomInvites_FinalizeInvite(EOS_HCustomInvites Handle, const EOS_CustomInvites_FinalizeInviteOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<CustomInvitesInterface>(Handle);
	return I ? I->FinalizeInvite() : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_CustomInvites_SendRequestToJoin(EOS_HCustomInvites Handle, const EOS_CustomInvites_SendRequestToJoinOptions* Options, void* ClientData, const EOS_CustomInvites_OnSendRequestToJoinCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<CustomInvitesInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Target = Options ? Options->TargetUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_CustomInvites_SendRequestToJoinCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData; Info.LocalUserId = Local; Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyRequestToJoinResponseReceived(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyRequestToJoinResponseReceivedOptions* Options, void* ClientData, const EOS_CustomInvites_OnRequestToJoinResponseReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->ReqResponse_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyRequestToJoinResponseReceived(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->ReqResponse_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyRequestToJoinReceived(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyRequestToJoinReceivedOptions* Options, void* ClientData, const EOS_CustomInvites_OnRequestToJoinReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->ReqReceived_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyRequestToJoinReceived(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->ReqReceived_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifySendCustomNativeInviteRequested(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifySendCustomNativeInviteRequestedOptions* Options, void* ClientData, const EOS_CustomInvites_OnSendCustomNativeInviteRequestedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Handle; (void)Options; (void)ClientData; (void)NotificationFn; return EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifySendCustomNativeInviteRequested(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); (void)Handle; (void)InId; }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyRequestToJoinAccepted(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyRequestToJoinAcceptedOptions* Options, void* ClientData, const EOS_CustomInvites_OnRequestToJoinAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->ReqAccepted_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyRequestToJoinAccepted(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->ReqAccepted_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_CustomInvites_AddNotifyRequestToJoinRejected(EOS_HCustomInvites Handle, const EOS_CustomInvites_AddNotifyRequestToJoinRejectedOptions* Options, void* ClientData, const EOS_CustomInvites_OnRequestToJoinRejectedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<CustomInvitesInterface>(Handle); return I ? I->ReqRejected_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_CustomInvites_RemoveNotifyRequestToJoinRejected(EOS_HCustomInvites Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<CustomInvitesInterface>(Handle)) I->ReqRejected_.Remove(InId); }

EOS_DECLARE_FUNC(void) EOS_CustomInvites_AcceptRequestToJoin(EOS_HCustomInvites Handle, const EOS_CustomInvites_AcceptRequestToJoinOptions* Options, void* ClientData, const EOS_CustomInvites_OnAcceptRequestToJoinCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<CustomInvitesInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Target = Options ? Options->TargetUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_CustomInvites_AcceptRequestToJoinCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData; Info.LocalUserId = Local; Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_CustomInvites_RejectRequestToJoin(EOS_HCustomInvites Handle, const EOS_CustomInvites_RejectRequestToJoinOptions* Options, void* ClientData, const EOS_CustomInvites_OnRejectRequestToJoinCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<CustomInvitesInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Target = Options ? Options->TargetUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_CustomInvites_RejectRequestToJoinCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData; Info.LocalUserId = Local; Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}
