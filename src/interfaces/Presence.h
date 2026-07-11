#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_presence_types.h"

namespace EOSEmu
{
	// A pending presence edit built by CreatePresenceModification and committed
	// by SetPresence. Mirrors the header's two-phase builder.
	struct PresenceModificationObj
	{
		bool bSetStatus = false; EOS_Presence_EStatus Status = EOS_Presence_EStatus::EOS_PS_Online;
		bool bSetRichText = false; std::string RichText;
		bool bSetJoinInfo = false; std::string JoinInfo;
		std::vector<std::pair<std::string, std::string>> SetData;
		std::vector<std::string> DeleteData;
	};

	// Presence for the local user is authoritative and replicated to peers in
	// the Hello presence extension (status, rich text, join info, data records,
	// product identity); remote users' presence is served back verbatim from
	// the peer directory, so a friend running the same game reads as in-game.
	class PresenceInterface : public InterfaceBase
	{
	public:
		explicit PresenceInterface(Platform& Owner) : InterfaceBase(Owner) {}

		bool HasPresence(EOS_EpicAccountId Target);
		EOS_EResult CopyPresence(EOS_EpicAccountId Target, EOS_Presence_Info** Out);
		void SetPresence(EOS_EpicAccountId Local, PresenceModificationObj* Mod, void* ClientData, EOS_Presence_SetPresenceCompleteCallback Cb);
		EOS_EResult GetJoinInfo(EOS_EpicAccountId Target, char* OutBuffer, int32_t* InOutLen);

		/// A peer's Hello carried different presence content. Runs on the
		/// receive thread; posts OnPresenceChanged observers to the Dispatcher.
		void OnRemotePresenceChanged(const std::string& EpicId);

		EOS_NotificationId AddNotifyOnPresenceChanged(EOS_Presence_OnPresenceChangedCallback Cb, void* ClientData) { return PresenceChanged_.Add(Cb, ClientData); }
		void RemoveNotifyOnPresenceChanged(EOS_NotificationId Id) { PresenceChanged_.Remove(Id); }
		EOS_NotificationId AddNotifyJoinGameAccepted(EOS_Presence_OnJoinGameAcceptedCallback Cb, void* ClientData) { return JoinGameAccepted_.Add(Cb, ClientData); }
		void RemoveNotifyJoinGameAccepted(EOS_NotificationId Id) { JoinGameAccepted_.Remove(Id); }

	private:
		std::mutex Mutex_;
		EOS_Presence_EStatus LocalStatus_ = EOS_Presence_EStatus::EOS_PS_Online;
		std::string LocalRichText_;
		std::string LocalJoinInfo_;
		std::map<std::string, std::string> LocalData_;

		NotifyRegistry<EOS_Presence_OnPresenceChangedCallback> PresenceChanged_;
		NotifyRegistry<EOS_Presence_OnJoinGameAcceptedCallback> JoinGameAccepted_;
	};
}
