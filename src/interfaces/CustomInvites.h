#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_custominvites_types.h"
#include "net/Lan.h"

namespace EOSEmu
{
	// Custom invites carry an application-defined payload between LAN peers.
	// SetCustomInvite stores the outgoing payload; SendCustomInvite unicasts it
	// to each target, whose CustomInviteReceived notification fires.
	class CustomInvitesInterface : public InterfaceBase
	{
	public:
		explicit CustomInvitesInterface(Platform& Owner) : InterfaceBase(Owner) {}

		EOS_EResult SetCustomInvite(const char* Payload);
		void SendCustomInvite(EOS_ProductUserId Local, const EOS_ProductUserId* Targets, uint32_t Count, void* ClientData, EOS_CustomInvites_OnSendCustomInviteCallback Cb);
		EOS_EResult FinalizeInvite();

		NotifyRegistry<EOS_CustomInvites_OnCustomInviteReceivedCallback> Received_;
		NotifyRegistry<EOS_CustomInvites_OnCustomInviteAcceptedCallback> Accepted_;
		NotifyRegistry<EOS_CustomInvites_OnCustomInviteRejectedCallback> Rejected_;
		NotifyRegistry<EOS_CustomInvites_OnRequestToJoinReceivedCallback> ReqReceived_;
		NotifyRegistry<EOS_CustomInvites_OnRequestToJoinResponseReceivedCallback> ReqResponse_;
		NotifyRegistry<EOS_CustomInvites_OnRequestToJoinAcceptedCallback> ReqAccepted_;
		NotifyRegistry<EOS_CustomInvites_OnRequestToJoinRejectedCallback> ReqRejected_;

		void OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len);

	private:
		std::mutex Mutex_;
		std::string OutgoingPayload_;
		uint64_t NextInvite_ = 1;
	};
}
