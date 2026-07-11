#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"
#include "interfaces/Attributes.h"

#include "eos_sessions_types.h"
#include "net/Lan.h"

namespace EOSEmu
{
	// --- internal record model ---------------------------------------------

	struct SessionAttr
	{
		AttrValue Value;
		EOS_ESessionAttributeAdvertisementType Adv = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
	};

	struct SessionRecord
	{
		std::string SessionName;   // caller-chosen local name
		std::string SessionId;     // backend id, globally meaningful
		std::string OwnerProductId;
		std::string BucketId;
		std::string HostAddress;   // application-defined; carries our LAN endpoint
		uint32_t MaxPlayers = 0;
		EOS_EOnlineSessionPermissionLevel Permission = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
		EOS_Bool bAllowJoinInProgress = EOS_TRUE;
		EOS_Bool bInvitesAllowed = EOS_TRUE;
		EOS_Bool bSanctionsEnabled = EOS_FALSE;
		EOS_Bool bPresenceEnabled = EOS_FALSE;
		std::vector<uint32_t> AllowedPlatformIds;
		std::vector<std::pair<std::string, SessionAttr>> Attributes;
		std::vector<std::string> RegisteredPlayers;
		EOS_EOnlineSessionState State = EOS_EOnlineSessionState::EOS_OSS_NoSession;
		net::Endpoint HostEndpoint;
		uint64_t Version = 0;
		uint64_t LastSeenTick = 0;

		uint32_t OpenSlots() const;
	};

	// --- handle objects ----------------------------------------------------

	struct SessionModificationObj
	{
		std::string SessionName;
		std::string LocalUser;
		bool bIsCreate = false;

		// Captured at CreateSessionModification.
		std::string SessionIdOverride;
		EOS_Bool bPresence = EOS_FALSE;
		EOS_Bool bSanctions = EOS_FALSE;

		bool bSetBucket = false; std::string Bucket;
		bool bSetHost = false; std::string HostAddress;
		bool bSetPerm = false; EOS_EOnlineSessionPermissionLevel Perm = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
		bool bSetJIP = false; EOS_Bool JIP = EOS_TRUE;
		bool bSetMax = false; uint32_t Max = 0;
		bool bSetInvites = false; EOS_Bool Invites = EOS_TRUE;
		bool bSetPlatforms = false; std::vector<uint32_t> Platforms;
		std::vector<std::pair<std::string, SessionAttr>> AddAttrs;
		std::vector<std::string> RemoveAttrs;
	};

	struct SessionDetailsObj { SessionRecord Record; };
	struct ActiveSessionObj { std::string SessionName; std::string LocalUser; };

	struct SessionSearchObj
	{
		uint32_t MaxResults = 200;
		std::string BySessionId;
		std::string ByTargetUser;
		struct Param { std::string Key; AttrValue Value; EOS_EComparisonOp Op; };
		std::vector<Param> Params;
		std::vector<SessionRecord> Results;
	};

	// Overlay social-panel rows (see src/overlay/Overlay.h).
	struct SessionInviteView { std::string InviteId; std::string SessionId; std::string FromPid; };
	struct SessionJoinableView { std::string SessionId; std::string OwnerPid; };

	// --- interface ---------------------------------------------------------

	class SessionsInterface : public InterfaceBase
	{
	public:
		explicit SessionsInterface(Platform& Owner) : InterfaceBase(Owner) {}

		EOS_EResult CreateSessionModification(const EOS_Sessions_CreateSessionModificationOptions* Options, EOS_HSessionModification* Out);
		EOS_EResult UpdateSessionModification(const EOS_Sessions_UpdateSessionModificationOptions* Options, EOS_HSessionModification* Out);
		void UpdateSession(const EOS_Sessions_UpdateSessionOptions* Options, void* ClientData, EOS_Sessions_OnUpdateSessionCallback Cb);
		void DestroySession(const EOS_Sessions_DestroySessionOptions* Options, void* ClientData, EOS_Sessions_OnDestroySessionCallback Cb);
		void JoinSession(const EOS_Sessions_JoinSessionOptions* Options, void* ClientData, EOS_Sessions_OnJoinSessionCallback Cb);
		void StartSession(const EOS_Sessions_StartSessionOptions* Options, void* ClientData, EOS_Sessions_OnStartSessionCallback Cb);
		void EndSession(const EOS_Sessions_EndSessionOptions* Options, void* ClientData, EOS_Sessions_OnEndSessionCallback Cb);
		void RegisterPlayers(const EOS_Sessions_RegisterPlayersOptions* Options, void* ClientData, EOS_Sessions_OnRegisterPlayersCallback Cb);
		void UnregisterPlayers(const EOS_Sessions_UnregisterPlayersOptions* Options, void* ClientData, EOS_Sessions_OnUnregisterPlayersCallback Cb);
		void SendInvite(const EOS_Sessions_SendInviteOptions* Options, void* ClientData, EOS_Sessions_OnSendInviteCallback Cb);
		void RejectInvite(const EOS_Sessions_RejectInviteOptions* Options, void* ClientData, EOS_Sessions_OnRejectInviteCallback Cb);
		void QueryInvites(const EOS_Sessions_QueryInvitesOptions* Options, void* ClientData, EOS_Sessions_OnQueryInvitesCallback Cb);

		EOS_EResult CreateSessionSearch(const EOS_Sessions_CreateSessionSearchOptions* Options, EOS_HSessionSearch* Out);
		EOS_EResult CopyActiveSessionHandle(const char* SessionName, EOS_HActiveSession* Out);
		EOS_EResult CopySessionHandleByInviteId(const char* InviteId, EOS_HSessionDetails* Out);
		EOS_EResult IsUserInSession(const char* SessionName, EOS_ProductUserId Target);
		EOS_EResult DumpSessionState(const char* SessionName);
		uint32_t GetInviteCount(EOS_ProductUserId Local);
		EOS_EResult GetInviteIdByIndex(EOS_ProductUserId Local, uint32_t Index, char* OutBuffer, int32_t* InOutLen);

		// --- overlay-driven invite-accept / join-friend flow ----------------
		std::vector<SessionInviteView> SnapshotInvites();
		std::vector<SessionJoinableView> SnapshotJoinable();
		void AcceptInviteFromOverlay(const std::string& InviteId);
		void RejectInviteFromOverlay(const std::string& InviteId);
		void BeginJoinFromOverlay(const std::string& SessionId);
		EOS_EResult CopyDetailsByUiEventId(uint64_t UiEventId, EOS_HSessionDetails* Out);
		void AcknowledgeUiEvent(uint64_t UiEventId);

		// Lookups used by handle-object entry points.
		bool CopyRecord(const std::string& SessionName, SessionRecord& Out);
		void CollectSearchResults(SessionSearchObj& Search);

		NotifyRegistry<EOS_Sessions_OnSessionInviteReceivedCallback> InviteReceived_;
		NotifyRegistry<EOS_Sessions_OnSessionInviteAcceptedCallback> InviteAccepted_;
		NotifyRegistry<EOS_Sessions_OnSessionInviteRejectedCallback> InviteRejected_;
		NotifyRegistry<EOS_Sessions_OnJoinSessionAcceptedCallback> JoinAccepted_;
		NotifyRegistry<EOS_Sessions_OnLeaveSessionRequestedCallback> LeaveRequested_;

		void OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len);
		void PeriodicAnnounce();

		std::mutex& Mutex() { return Mutex_; }

	private:
		std::string LocalPid() const;
		const char* Intern(const std::string& S);
		std::string NewSessionId(const std::string& OwnerPid);
		void BroadcastAnnounce(const SessionRecord& Rec);
		void SerializeRecord(net::ByteWriter& W, const SessionRecord& Rec);
		bool DeserializeRecord(net::ByteReader& R, SessionRecord& Out);
		bool MatchesSearch(const SessionSearchObj& Search, const SessionRecord& Rec) const;

		std::mutex Mutex_;
		std::map<std::string, SessionRecord> Local_;   // sessions we host/participate, by name
		std::map<std::string, SessionRecord> Known_;   // discovered sessions, by SessionId
		std::set<std::string> Owned_;                  // session names we host

		struct InviteRec { std::string InviteId; std::string SessionId; std::string FromProduct; };
		std::vector<InviteRec> Invites_;
		uint64_t NextInvite_ = 1;
		uint64_t SessionCounter_ = 1;
		std::set<std::string> Interned_;

		// UiEventId -> session snapshot (see LobbyInterface::UiEvents_).
		std::map<uint64_t, SessionRecord> UiEvents_;

		// True if a session we participate in (Local_) carries this backend id.
		bool ParticipatingInId(const std::string& SessionId) const;
	};
}
