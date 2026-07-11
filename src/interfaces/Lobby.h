#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"
#include "interfaces/Attributes.h"

#include "eos_lobby_types.h"
#include "net/Lan.h"

namespace EOSEmu
{
	// --- internal record model ---------------------------------------------

	struct LobbyAttr
	{
		AttrValue Value;
		EOS_ELobbyAttributeVisibility Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
	};

	struct LobbyMemberRec
	{
		std::string ProductId;
		EOS_OnlinePlatformType Platform = EOS_OPT_Epic;
		// Ordered so index enumeration is stable across calls.
		std::vector<std::pair<std::string, LobbyAttr>> Attributes;
	};

	struct LobbyRecord
	{
		std::string LobbyId;
		std::string OwnerProductId;
		std::string BucketId;
		uint32_t MaxMembers = 0;
		EOS_ELobbyPermissionLevel Permission = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
		EOS_Bool bAllowInvites = EOS_TRUE;
		EOS_Bool bAllowHostMigration = EOS_TRUE;
		EOS_Bool bRTCRoomEnabled = EOS_FALSE;
		EOS_Bool bPresenceEnabled = EOS_FALSE;
		EOS_Bool bAllowJoinById = EOS_FALSE;
		std::vector<std::pair<std::string, LobbyAttr>> Attributes; // lobby-level
		std::vector<LobbyMemberRec> Members;
		std::vector<uint32_t> AllowedPlatformIds;
		net::Endpoint HostEndpoint;
		uint64_t Version = 0;
		uint64_t LastSeenTick = 0;

		LobbyMemberRec* FindMember(const std::string& Pid);
		const LobbyMemberRec* FindMember(const std::string& Pid) const;
		uint32_t AvailableSlots() const;
	};

	// --- handle objects ----------------------------------------------------

	struct LobbyModificationObj
	{
		std::string LobbyId;
		std::string LocalUser;

		bool bSetBucket = false; std::string Bucket;
		bool bSetPerm = false; EOS_ELobbyPermissionLevel Perm = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
		bool bSetMax = false; uint32_t Max = 0;
		bool bSetInvites = false; EOS_Bool Invites = EOS_TRUE;
		bool bSetPlatforms = false; std::vector<uint32_t> Platforms;

		std::vector<std::pair<std::string, LobbyAttr>> AddAttrs;
		std::vector<std::string> RemoveAttrs;
		std::vector<std::pair<std::string, LobbyAttr>> AddMemberAttrs;
		std::vector<std::string> RemoveMemberAttrs;
	};

	struct LobbyDetailsObj
	{
		LobbyRecord Record;
		std::string LocalUser;
	};

	struct LobbySearchObj
	{
		uint32_t MaxResults = EOS_LOBBY_MAX_SEARCH_RESULTS;
		std::string ByLobbyId;
		std::string ByTargetUser;
		struct Param { std::string Key; AttrValue Value; EOS_EComparisonOp Op; };
		std::vector<Param> Params;
		std::vector<LobbyRecord> Results;
	};

	// Overlay social-panel rows (see src/overlay/Overlay.h). Plain snapshots the
	// UI interface pulls once per Tick; no interned pointers cross the boundary.
	struct LobbyInviteView { std::string InviteId; std::string LobbyId; std::string FromPid; };
	struct LobbyJoinableView { std::string LobbyId; std::string OwnerPid; };

	// One RTC-enabled lobby we belong to, as the RTC shim's pump sees it (see
	// src/interfaces/RTC.h). RoomName matches EOS_Lobby_GetRTCRoomName.
	struct LobbyRTCRoomView { std::string LobbyId; std::string RoomName; std::vector<std::string> MemberPids; };

	// --- interface ---------------------------------------------------------

	class LobbyInterface : public InterfaceBase
	{
	public:
		explicit LobbyInterface(Platform& Owner) : InterfaceBase(Owner) {}

		// Async lifecycle
		void CreateLobby(const EOS_Lobby_CreateLobbyOptions* Options, void* ClientData, EOS_Lobby_OnCreateLobbyCallback Cb);
		void DestroyLobby(const EOS_Lobby_DestroyLobbyOptions* Options, void* ClientData, EOS_Lobby_OnDestroyLobbyCallback Cb);
		void JoinLobby(const EOS_Lobby_JoinLobbyOptions* Options, void* ClientData, EOS_Lobby_OnJoinLobbyCallback Cb);
		void JoinLobbyById(const EOS_Lobby_JoinLobbyByIdOptions* Options, void* ClientData, EOS_Lobby_OnJoinLobbyByIdCallback Cb);
		void LeaveLobby(const EOS_Lobby_LeaveLobbyOptions* Options, void* ClientData, EOS_Lobby_OnLeaveLobbyCallback Cb);
		void UpdateLobby(const EOS_Lobby_UpdateLobbyOptions* Options, void* ClientData, EOS_Lobby_OnUpdateLobbyCallback Cb);
		void PromoteMember(const EOS_Lobby_PromoteMemberOptions* Options, void* ClientData, EOS_Lobby_OnPromoteMemberCallback Cb);
		void KickMember(const EOS_Lobby_KickMemberOptions* Options, void* ClientData, EOS_Lobby_OnKickMemberCallback Cb);
		void HardMuteMember(const EOS_Lobby_HardMuteMemberOptions* Options, void* ClientData, EOS_Lobby_OnHardMuteMemberCallback Cb);
		void SendInvite(const EOS_Lobby_SendInviteOptions* Options, void* ClientData, EOS_Lobby_OnSendInviteCallback Cb);
		void RejectInvite(const EOS_Lobby_RejectInviteOptions* Options, void* ClientData, EOS_Lobby_OnRejectInviteCallback Cb);
		void QueryInvites(const EOS_Lobby_QueryInvitesOptions* Options, void* ClientData, EOS_Lobby_OnQueryInvitesCallback Cb);

		// Sync
		EOS_EResult UpdateLobbyModification(const EOS_Lobby_UpdateLobbyModificationOptions* Options, EOS_HLobbyModification* Out);
		EOS_EResult CreateLobbySearch(const EOS_Lobby_CreateLobbySearchOptions* Options, EOS_HLobbySearch* Out);
		EOS_EResult CopyLobbyDetailsHandle(const EOS_Lobby_CopyLobbyDetailsHandleOptions* Options, EOS_HLobbyDetails* Out);
		EOS_EResult CopyLobbyDetailsHandleByInviteId(const char* InviteId, EOS_HLobbyDetails* Out);
		uint32_t GetInviteCount(EOS_ProductUserId LocalUser);
		EOS_EResult GetInviteIdByIndex(EOS_ProductUserId LocalUser, uint32_t Index, char* OutBuffer, int32_t* InOutLen);
		EOS_EResult GetRTCRoomName(const EOS_Lobby_GetRTCRoomNameOptions* Options, char* OutBuffer, uint32_t* InOutLen);
		EOS_EResult GetConnectString(const EOS_Lobby_GetConnectStringOptions* Options, char* OutBuffer, uint32_t* InOutLen);

		// --- overlay-driven invite-accept / join-friend flow ----------------
		// Snapshots for the overlay social panel (and headless auto-accept):
		// pending invites whose lobby we haven't joined, and discovered lobbies
		// we could join. Both skip lobbies already in MemberOf_/Owned_.
		std::vector<LobbyInviteView> SnapshotInvites();
		std::vector<LobbyJoinableView> SnapshotJoinable();
		// The user accepted a received invite in the overlay: fire InviteAccepted
		// (the game then resolves it via CopyLobbyDetailsHandleByInviteId + Join).
		void AcceptInviteFromOverlay(const std::string& InviteId);
		// The user rejected a received invite: drop it and fire InviteRejected.
		void RejectInviteFromOverlay(const std::string& InviteId);
		// The user chose "join" on a discovered lobby: mint a UiEventId bound to a
		// snapshot of that lobby and fire JoinLobbyAccepted; the game resolves the
		// event via CopyLobbyDetailsHandleByUiEventId.
		void BeginJoinFromOverlay(const std::string& LobbyId);
		EOS_EResult CopyDetailsByUiEventId(uint64_t UiEventId, EOS_HLobbyDetails* Out);
		void AcknowledgeUiEvent(uint64_t UiEventId);

		// RTC-enabled lobbies we are currently a member of, for the RTC shim's
		// once-per-Tick reconciliation (RTCInterface::PumpTick).
		std::vector<LobbyRTCRoomView> SnapshotRTCRooms();

		// Notifications
		NotifyRegistry<EOS_Lobby_OnLobbyUpdateReceivedCallback> UpdateReceived_;
		NotifyRegistry<EOS_Lobby_OnLobbyMemberUpdateReceivedCallback> MemberUpdateReceived_;
		NotifyRegistry<EOS_Lobby_OnLobbyMemberStatusReceivedCallback> MemberStatusReceived_;
		NotifyRegistry<EOS_Lobby_OnLobbyInviteReceivedCallback> InviteReceived_;
		NotifyRegistry<EOS_Lobby_OnLobbyInviteAcceptedCallback> InviteAccepted_;
		NotifyRegistry<EOS_Lobby_OnLobbyInviteRejectedCallback> InviteRejected_;
		NotifyRegistry<EOS_Lobby_OnJoinLobbyAcceptedCallback> JoinAccepted_;
		NotifyRegistry<EOS_Lobby_OnLeaveLobbyRequestedCallback> LeaveRequested_;
		NotifyRegistry<EOS_Lobby_OnRTCRoomConnectionChangedCallback> RTCRoomConn_;

		// Receive-thread hook + periodic owner re-announce (called from Tick).
		void OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len);
		void PeriodicAnnounce();

		// Fills Search.Results from the known-lobby cache. Used by LobbySearch_Find.
		void CollectSearchResults(LobbySearchObj& Search);

		std::mutex& Mutex() { return Mutex_; }

	private:
		std::string LocalPid() const;
		const char* Intern(const std::string& S);
		std::string NewLobbyId(const std::string& OwnerPid);
		void BroadcastAnnounce(const LobbyRecord& Rec);
		void SerializeRecord(net::ByteWriter& W, const LobbyRecord& Rec);
		bool DeserializeRecord(net::ByteReader& R, LobbyRecord& Out);
		bool MatchesSearch(const LobbySearchObj& Search, const LobbyRecord& Rec) const;
		void ApplyModification(LobbyRecord& Rec, const LobbyModificationObj& Mod);

		std::mutex Mutex_;
		std::unordered_map<std::string, LobbyRecord> Known_; // all lobbies seen
		std::set<std::string> MemberOf_;                     // lobby ids we belong to
		std::set<std::string> Owned_;                        // lobby ids we own

		struct InviteRec { std::string InviteId; std::string LobbyId; std::string FromProduct; };
		std::vector<InviteRec> Invites_;
		uint64_t NextInvite_ = 1;
		uint64_t LobbyCounter_ = 1;

		// UiEventId -> lobby snapshot, minted by BeginJoinFromOverlay and resolved
		// by CopyDetailsByUiEventId. Freed by AcknowledgeUiEvent; a game that
		// never acknowledges leaks at most one entry per join click.
		std::map<uint64_t, LobbyRecord> UiEvents_;

		// Stable storage for const char* handed back to the consumer (lobby ids,
		// invite ids). std::set never invalidates element addresses.
		std::set<std::string> Interned_;
	};
}
