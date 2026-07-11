//
// RTC core -- room lifecycle, participants, settings. See RTC.h for the shim
// design. Audio/data entry points live in RTCAudio.cpp / RTCData.cpp.
//

#include "interfaces/RTC.h"
#include "interfaces/Lobby.h"

#include "core/Ids.h"
#include "core/Identity.h"
#include "core/Logging.h"
#include "core/Platform.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace EOSEmu
{
	RTCInterface::RTCInterface(Platform& Owner)
		: InterfaceBase(Owner)
	{
		Audio_ = std::make_unique<RTCAudioInterface>(Owner, *this);
		Data_ = std::make_unique<RTCDataInterface>(Owner, *this);
	}

	// --- fire helpers --------------------------------------------------------
	// Each posts one task that snapshots its registry at fire time, so handlers
	// registered earlier in the same drain (e.g. inside a CreateLobby completion)
	// still see the event.

	void RTCInterface::FireParticipantStatus(const std::string& Room, const std::string& Pid, bool Joined)
	{
		const std::string Local = Platform_.LocalIdentity().ProductUserIdString();
		Platform_.Dispatch().Post([this, Room, Pid, Joined, Local]
		{
			for (const auto& E : ParticipantStatus_.Snapshot(Room))
			{
				EOS_RTC_ParticipantStatusChangedCallbackInfo Info = {};
				Info.ClientData = E.ClientData;
				Info.LocalUserId = Ids::InternProduct(Local);
				Info.RoomName = Room.c_str();
				Info.ParticipantId = Ids::InternProduct(Pid);
				Info.ParticipantStatus = Joined ? EOS_ERTCParticipantStatus::EOS_RTCPS_Joined
				                                : EOS_ERTCParticipantStatus::EOS_RTCPS_Left;
				Info.bParticipantInBlocklist = EOS_FALSE;
				E.Fn(&Info);
			}
		});
	}

	void RTCInterface::FireAudioParticipantUpdated(const std::string& Room, const std::string& Pid, EOS_ERTCAudioStatus Status)
	{
		const std::string Local = Platform_.LocalIdentity().ProductUserIdString();
		Platform_.Dispatch().Post([this, Room, Pid, Status, Local]
		{
			for (const auto& E : Audio_->ParticipantUpdated_.Snapshot(Room))
			{
				EOS_RTCAudio_ParticipantUpdatedCallbackInfo Info = {};
				Info.ClientData = E.ClientData;
				Info.LocalUserId = Ids::InternProduct(Local);
				Info.RoomName = Room.c_str();
				Info.ParticipantId = Ids::InternProduct(Pid);
				Info.bSpeaking = EOS_FALSE;   // no audio frames ever flow
				Info.AudioStatus = Status;
				E.Fn(&Info);
			}
		});
	}

	void RTCInterface::FireDataParticipantUpdated(const std::string& Room, const std::string& Pid)
	{
		const std::string Local = Platform_.LocalIdentity().ProductUserIdString();
		Platform_.Dispatch().Post([this, Room, Pid, Local]
		{
			for (const auto& E : Data_->ParticipantUpdated_.Snapshot(Room))
			{
				EOS_RTCData_ParticipantUpdatedCallbackInfo Info = {};
				Info.ClientData = E.ClientData;
				Info.LocalUserId = Ids::InternProduct(Local);
				Info.RoomName = Room.c_str();
				Info.ParticipantId = Ids::InternProduct(Pid);
				Info.DataStatus = EOS_ERTCDataStatus::EOS_RTCDS_Enabled;
				E.Fn(&Info);
			}
		});
	}

	void RTCInterface::FireLobbyRoomConnection(const std::string& LobbyId, bool Connected)
	{
		const std::string Local = Platform_.LocalIdentity().ProductUserIdString();
		Platform_.Dispatch().Post([this, LobbyId, Connected, Local]
		{
			for (const auto& E : Platform_.Lobby().RTCRoomConn_.Snapshot())
			{
				EOS_Lobby_RTCRoomConnectionChangedCallbackInfo Info = {};
				Info.ClientData = E.ClientData;
				Info.LobbyId = LobbyId.c_str();
				Info.LocalUserId = Ids::InternProduct(Local);
				Info.bIsConnected = Connected ? EOS_TRUE : EOS_FALSE;
				// "The room was left locally" (lobby left/destroyed) is EOS_Success.
				Info.DisconnectReason = EOS_EResult::EOS_Success;
				E.Fn(&Info);
			}
		});
	}

	void RTCInterface::FireParticipantJoinedBundle(const std::string& Room, const std::string& Pid)
	{
		FireParticipantStatus(Room, Pid, true);
		FireAudioParticipantUpdated(Room, Pid, EOS_ERTCAudioStatus::EOS_RTCAS_Disabled);
		FireDataParticipantUpdated(Room, Pid);
	}

	// --- room state ----------------------------------------------------------

	void RTCInterface::PumpTick()
	{
		const std::vector<LobbyRTCRoomView> Views = Platform_.Lobby().SnapshotRTCRooms();

		std::lock_guard<std::mutex> Lock(Mutex_);

		// New rooms and membership churn.
		for (const LobbyRTCRoomView& View : Views)
		{
			auto It = Rooms_.find(View.RoomName);
			if (It == Rooms_.end())
			{
				Room R;
				R.LobbyManaged = true;
				R.LobbyId = View.LobbyId;
				R.Participants = View.MemberPids;
				Rooms_.emplace(View.RoomName, std::move(R));
				EOSEMU_INFO(RTC, "joined lobby RTC room '%s' (%zu participant(s))",
					View.RoomName.c_str(), View.MemberPids.size());
				FireLobbyRoomConnection(View.LobbyId, true);
				for (const std::string& Pid : View.MemberPids)
				{
					FireParticipantJoinedBundle(View.RoomName, Pid);
				}
				continue;
			}
			Room& R = It->second;
			for (const std::string& Pid : View.MemberPids)
			{
				if (std::find(R.Participants.begin(), R.Participants.end(), Pid) == R.Participants.end())
				{
					FireParticipantJoinedBundle(View.RoomName, Pid);
				}
			}
			for (const std::string& Pid : R.Participants)
			{
				if (std::find(View.MemberPids.begin(), View.MemberPids.end(), Pid) == View.MemberPids.end())
				{
					FireParticipantStatus(View.RoomName, Pid, false);
				}
			}
			R.Participants = View.MemberPids;
		}

		// Rooms whose lobby we left (or that was destroyed).
		for (auto It = Rooms_.begin(); It != Rooms_.end();)
		{
			if (!It->second.LobbyManaged)
			{
				++It;
				continue;
			}
			const bool Alive = std::any_of(Views.begin(), Views.end(),
				[&](const LobbyRTCRoomView& V) { return V.RoomName == It->first; });
			if (Alive)
			{
				++It;
				continue;
			}
			EOSEMU_INFO(RTC, "left lobby RTC room '%s'", It->first.c_str());
			FireLobbyRoomConnection(It->second.LobbyId, false);
			// No participant Left events for the local user's own departure.
			It = Rooms_.erase(It);
		}
	}

	bool RTCInterface::IsInRoom(const std::string& RoomName)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		return Rooms_.count(RoomName) != 0;
	}

	bool RTCInterface::IsLobbyManagedRoom(const std::string& RoomName)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Rooms_.find(RoomName);
		return It != Rooms_.end() && It->second.LobbyManaged;
	}

	bool RTCInterface::RoomHasParticipant(const std::string& RoomName, const std::string& Pid)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Rooms_.find(RoomName);
		if (It == Rooms_.end()) return false;
		const auto& P = It->second.Participants;
		return std::find(P.begin(), P.end(), Pid) != P.end();
	}

	EOS_EResult RTCInterface::JoinCustomRoom(const std::string& RoomName)
	{
		const std::string Local = Platform_.LocalIdentity().ProductUserIdString();
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Rooms_.find(RoomName);
			if (It != Rooms_.end())
			{
				// Already-joined lobby rooms are the lobby system's business.
				return It->second.LobbyManaged ? EOS_EResult::EOS_AccessDenied
				                               : EOS_EResult::EOS_Success;
			}
			Room R;
			R.Participants.push_back(Local);
			Rooms_.emplace(RoomName, std::move(R));
		}
		EOSEMU_INFO(RTC, "joined custom RTC room '%s'", RoomName.c_str());
		FireParticipantJoinedBundle(RoomName, Local);
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult RTCInterface::LeaveCustomRoom(const std::string& RoomName)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Rooms_.find(RoomName);
		if (It == Rooms_.end()) return EOS_EResult::EOS_NotFound;
		if (It->second.LobbyManaged) return EOS_EResult::EOS_AccessDenied;
		Rooms_.erase(It);
		EOSEMU_INFO(RTC, "left custom RTC room '%s'", RoomName.c_str());
		return EOS_EResult::EOS_Success;
	}

	bool RTCInterface::SetLocalAudioStatus(const std::string& RoomName, EOS_ERTCAudioStatus Status)
	{
		std::string Local = Platform_.LocalIdentity().ProductUserIdString();
		bool Changed = false;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Rooms_.find(RoomName);
			if (It == Rooms_.end()) return false;
			Changed = It->second.LocalAudioStatus != Status;
			It->second.LocalAudioStatus = Status;
		}
		if (Changed)
		{
			FireAudioParticipantUpdated(RoomName, Local, Status);
		}
		return true;
	}

	namespace
	{
		// The four setting names eos_rtc_types.h documents, each taking a
		// boolean-ish string. Unknown name -> NotFound, bad value ->
		// InvalidParameters, exactly as the header specifies.
		EOS_EResult ValidateSetting(const char* Name, const char* Value)
		{
			if (Name == nullptr || Value == nullptr) return EOS_EResult::EOS_InvalidParameters;
			static const char* Known[] = {
				"DisableEchoCancelation", "DisableNoiseSupression",
				"DisableAutoGainControl", "DisableDtx",
			};
			bool Recognised = false;
			for (const char* K : Known)
			{
				if (std::strcmp(Name, K) == 0) { Recognised = true; break; }
			}
			if (!Recognised) return EOS_EResult::EOS_NotFound;
			auto EqualsNoCase = [](const char* A, const char* B)
			{
				for (; *A && *B; ++A, ++B)
				{
					if (std::tolower(static_cast<unsigned char>(*A)) != std::tolower(static_cast<unsigned char>(*B))) return false;
				}
				return *A == *B;
			};
			if (!EqualsNoCase(Value, "true") && !EqualsNoCase(Value, "false"))
			{
				return EOS_EResult::EOS_InvalidParameters;
			}
			return EOS_EResult::EOS_Success;
		}
	}
}

using namespace EOSEmu;

// ----------------------------------------------------------------------------
// Sub-interface handles.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_HRTCAudio) EOS_RTC_GetAudioInterface(EOS_HRTC Handle)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	return I ? reinterpret_cast<EOS_HRTCAudio>(&I->Audio()) : nullptr;
}

EOS_DECLARE_FUNC(EOS_HRTCData) EOS_RTC_GetDataInterface(EOS_HRTC Handle)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	return I ? reinterpret_cast<EOS_HRTCData>(&I->Data()) : nullptr;
}

// ----------------------------------------------------------------------------
// Join / leave / block.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_RTC_JoinRoom(EOS_HRTC Handle, const EOS_RTC_JoinRoomOptions* Options, void* ClientData, const EOS_RTC_OnJoinRoomCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTC_JOINROOM_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (I->IsLobbyManagedRoom(RoomName))
	{
		// "the lobby system will automatically join and leave RTC Rooms";
		// joining one by hand is documented AccessDenied.
		Result = EOS_EResult::EOS_AccessDenied;
	}
	else
	{
		Result = I->JoinCustomRoom(RoomName);
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, RoomName]
	{
		EOS_RTC_JoinRoomCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTC_LeaveRoom(EOS_HRTC Handle, const EOS_RTC_LeaveRoomOptions* Options, void* ClientData, const EOS_RTC_OnLeaveRoomCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTC_LEAVEROOM_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else
	{
		Result = I->LeaveCustomRoom(RoomName);
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, RoomName]
	{
		EOS_RTC_LeaveRoomCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_RTC_BlockParticipant(EOS_HRTC Handle, const EOS_RTC_BlockParticipantOptions* Options, void* ClientData, const EOS_RTC_OnBlockParticipantCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EResult Result = EOS_EResult::EOS_Success;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	EOS_ProductUserId Participant = Options ? Options->ParticipantId : nullptr;
	EOS_Bool Blocked = Options ? Options->bBlocked : EOS_FALSE;
	std::string RoomName = (Options && Options->RoomName) ? Options->RoomName : "";
	if (Options == nullptr || RoomName.empty() || Local == nullptr || Participant == nullptr)
	{
		Result = EOS_EResult::EOS_InvalidParameters;
	}
	else if (!VersionInRange(Options->ApiVersion, EOS_RTC_BLOCKPARTICIPANT_API_LATEST))
	{
		Result = EOS_EResult::EOS_IncompatibleVersion;
	}
	else if (!I->RoomHasParticipant(RoomName, Ids::ProductString(Participant)))
	{
		Result = EOS_EResult::EOS_NotFound;
	}
	// No media is exchanged either way, so "blocking" needs no further effect.

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Result, Local, Participant, Blocked, RoomName]
	{
		EOS_RTC_BlockParticipantCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RoomName = RoomName.empty() ? nullptr : RoomName.c_str();
		Info.ParticipantId = Participant;
		Info.bBlocked = Blocked;
		CompletionDelegate(&Info);
	});
}

// ----------------------------------------------------------------------------
// Notifications.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTC_AddNotifyDisconnected(EOS_HRTC Handle, const EOS_RTC_AddNotifyDisconnectedOptions* Options, void* ClientData, const EOS_RTC_OnDisconnectedCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	// Documented: always invalid for a lobby-managed room (use
	// EOS_Lobby_AddNotifyRTCRoomConnectionChanged for those).
	if (I->IsLobbyManagedRoom(Options->RoomName)) return EOS_INVALID_NOTIFICATIONID;
	return I->Disconnected_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTC_RemoveNotifyDisconnected(EOS_HRTC Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCInterface>(Handle)) I->Disconnected_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTC_AddNotifyParticipantStatusChanged(EOS_HRTC Handle, const EOS_RTC_AddNotifyParticipantStatusChangedOptions* Options, void* ClientData, const EOS_RTC_OnParticipantStatusChangedCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	return I->ParticipantStatus_.Add(CompletionDelegate, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTC_RemoveNotifyParticipantStatusChanged(EOS_HRTC Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCInterface>(Handle)) I->ParticipantStatus_.Remove(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTC_AddNotifyRoomStatisticsUpdated(EOS_HRTC Handle, const EOS_RTC_AddNotifyRoomStatisticsUpdatedOptions* Options, void* ClientData, const EOS_RTC_OnRoomStatisticsUpdatedCallback StatisticsUpdateHandler)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->RoomName == nullptr) return EOS_INVALID_NOTIFICATIONID;
	// Registered but never fired -- there is no media session to measure.
	return I->RoomStats_.Add(StatisticsUpdateHandler, ClientData, Options->RoomName);
}

EOS_DECLARE_FUNC(void) EOS_RTC_RemoveNotifyRoomStatisticsUpdated(EOS_HRTC Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<RTCInterface>(Handle)) I->RoomStats_.Remove(NotificationId);
}

// ----------------------------------------------------------------------------
// Settings.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTC_SetSetting(EOS_HRTC Handle, const EOS_RTC_SetSettingOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	if (!VersionInRange(Options->ApiVersion, EOS_RTC_SETSETTING_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	// Accepted and ignored; there is no audio pipeline to configure.
	return ValidateSetting(Options->SettingName, Options->SettingValue);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTC_SetRoomSetting(EOS_HRTC Handle, const EOS_RTC_SetRoomSettingOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<RTCInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	if (!VersionInRange(Options->ApiVersion, EOS_RTC_SETROOMSETTING_API_LATEST)) return EOS_EResult::EOS_IncompatibleVersion;
	if (Options->LocalUserId == nullptr || Options->RoomName == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return ValidateSetting(Options->SettingName, Options->SettingValue);
}
