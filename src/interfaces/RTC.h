#pragma once

//
// RTC voice/data shim -- rooms and rosters are real, media is not.
//
// A voice-enabled game couples RTC rooms to lobbies: EOS_Lobby_GetRTCRoomName
// hands it a room name and the lobby system joins/leaves that room for it.
// Today's generated stubs post EOS_NotImplemented, so the game's "connecting
// voice..." state never resolves. This shim makes the *lifecycle* real:
//
//  - Lobby-managed rooms are reconciled once per Tick (PumpTick) against the
//    lobbies we belong to that have bEnableRTCRoom. Joining such a lobby fires
//    EOS_Lobby_AddNotifyRTCRoomConnectionChanged (connected) plus
//    ParticipantStatusChanged(Joined) for every member; member churn and
//    leaving follow the same path. EOS_RTC_JoinRoom on such a room returns
//    EOS_AccessDenied exactly as the header documents.
//  - Custom rooms (JoinRoom on a non-lobby name) succeed with the local user
//    as the only participant; there is no LAN model for them.
//  - Every participant is reported present with bSpeaking=false and
//    AudioStatus=EOS_RTCAS_Disabled (muted): honest for a shim that never
//    moves audio frames, and a game's own mute toggle still round-trips via
//    EOS_RTCAudio_UpdateSending. No audio or data payload ever flows.
//
// All notifications obey the Tick-thread rule: posted via Dispatch(), and the
// registry is snapshotted at fire time so a handler registered inside a
// CreateLobby/JoinLobby completion (the pattern eos_rtc.h documents) still
// receives the initial Joined events queued in the same drain.
//

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_rtc.h"
#include "eos_rtc_audio.h"
#include "eos_rtc_data.h"

namespace EOSEmu
{
	/// NotifyRegistry variant whose entries are scoped to one room name, since
	/// every RTC AddNotify* takes a RoomName and the real SDK only fires the
	/// handler for events in that room.
	template <typename FnPtr>
	class RoomNotifyRegistry
	{
	public:
		struct Entry
		{
			EOS_NotificationId Id;
			FnPtr Fn;
			void* ClientData;
			std::string Room;
		};

		EOS_NotificationId Add(FnPtr Fn, void* ClientData, const std::string& Room)
		{
			if (Fn == nullptr || Room.empty())
			{
				return EOS_INVALID_NOTIFICATIONID;
			}
			const EOS_NotificationId Id = NextNotificationId();
			std::lock_guard<std::mutex> Lock(Mutex_);
			Entries_.push_back(Entry{Id, Fn, ClientData, Room});
			return Id;
		}

		void Remove(EOS_NotificationId Id)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (auto It = Entries_.begin(); It != Entries_.end(); ++It)
			{
				if (It->Id == Id)
				{
					Entries_.erase(It);
					return;
				}
			}
		}

		std::vector<Entry> Snapshot(const std::string& Room) const
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			std::vector<Entry> Out;
			for (const Entry& E : Entries_)
			{
				if (E.Room == Room) Out.push_back(E);
			}
			return Out;
		}

	private:
		mutable std::mutex Mutex_;
		std::vector<Entry> Entries_;
	};

	class RTCInterface;

	/// EOS_HRTCAudio points here. Owns only its notification registries; room
	/// state lives in the parent RTCInterface.
	class RTCAudioInterface : public InterfaceBase
	{
	public:
		RTCAudioInterface(Platform& Owner, RTCInterface& RTC) : InterfaceBase(Owner), RTC_(RTC) {}
		RTCInterface& RTC() { return RTC_; }

		RoomNotifyRegistry<EOS_RTCAudio_OnParticipantUpdatedCallback> ParticipantUpdated_;
		NotifyRegistry<EOS_RTCAudio_OnAudioDevicesChangedCallback> DevicesChanged_;
		RoomNotifyRegistry<EOS_RTCAudio_OnAudioInputStateCallback> InputState_;
		RoomNotifyRegistry<EOS_RTCAudio_OnAudioOutputStateCallback> OutputState_;
		RoomNotifyRegistry<EOS_RTCAudio_OnAudioBeforeSendCallback> BeforeSend_;
		RoomNotifyRegistry<EOS_RTCAudio_OnAudioBeforeRenderCallback> BeforeRender_;

	private:
		RTCInterface& RTC_;
	};

	/// EOS_HRTCData points here. Same shape as RTCAudioInterface.
	class RTCDataInterface : public InterfaceBase
	{
	public:
		RTCDataInterface(Platform& Owner, RTCInterface& RTC) : InterfaceBase(Owner), RTC_(RTC) {}
		RTCInterface& RTC() { return RTC_; }

		RoomNotifyRegistry<EOS_RTCData_OnDataReceivedCallback> DataReceived_;
		RoomNotifyRegistry<EOS_RTCData_OnParticipantUpdatedCallback> ParticipantUpdated_;

	private:
		RTCInterface& RTC_;
	};

	class RTCInterface : public InterfaceBase
	{
	public:
		explicit RTCInterface(Platform& Owner);

		RTCAudioInterface& Audio() { return *Audio_; }
		RTCDataInterface& Data() { return *Data_; }

		struct Room
		{
			bool LobbyManaged = false;
			std::string LobbyId;                       // set when LobbyManaged
			std::vector<std::string> Participants;     // product ids, local included
			EOS_ERTCAudioStatus LocalAudioStatus = EOS_ERTCAudioStatus::EOS_RTCAS_Disabled;
		};

		/// Reconciles lobby-managed rooms against LobbyInterface once per Tick
		/// (called from Platform::Tick before the dispatcher drains, so the
		/// notifications land in the same drain as the lobby completion that
		/// caused them).
		void PumpTick();

		bool IsInRoom(const std::string& RoomName);
		bool IsLobbyManagedRoom(const std::string& RoomName);
		bool RoomHasParticipant(const std::string& RoomName, const std::string& Pid);

		/// JoinRoom/LeaveRoom for non-lobby rooms. Join fires the local Joined
		/// participant bundle; Leave fires nothing (the header documents that no
		/// participant event is raised when the local user leaves).
		EOS_EResult JoinCustomRoom(const std::string& RoomName);
		EOS_EResult LeaveCustomRoom(const std::string& RoomName);

		/// UpdateSending state; fires an audio ParticipantUpdated for the local
		/// user when the status actually changes. Returns false if not in room.
		bool SetLocalAudioStatus(const std::string& RoomName, EOS_ERTCAudioStatus Status);

		RoomNotifyRegistry<EOS_RTC_OnDisconnectedCallback> Disconnected_;
		RoomNotifyRegistry<EOS_RTC_OnParticipantStatusChangedCallback> ParticipantStatus_;
		RoomNotifyRegistry<EOS_RTC_OnRoomStatisticsUpdatedCallback> RoomStats_;

	private:
		// All Fire* helpers only post to the dispatcher; the registries are
		// snapshotted inside the posted task (fire time), not here.
		void FireParticipantStatus(const std::string& Room, const std::string& Pid, bool Joined);
		void FireAudioParticipantUpdated(const std::string& Room, const std::string& Pid, EOS_ERTCAudioStatus Status);
		void FireDataParticipantUpdated(const std::string& Room, const std::string& Pid);
		void FireLobbyRoomConnection(const std::string& LobbyId, bool Connected);
		// Joined + audio(muted) + data(enabled) for one participant.
		void FireParticipantJoinedBundle(const std::string& Room, const std::string& Pid);

		std::mutex Mutex_;
		std::map<std::string, Room> Rooms_;

		std::unique_ptr<RTCAudioInterface> Audio_;
		std::unique_ptr<RTCDataInterface> Data_;
	};
}
