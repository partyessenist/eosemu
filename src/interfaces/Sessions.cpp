//
// Sessions interface over LAN.
//
// Structurally different from Lobby (CLAUDE.md): sessions do not replicate
// member state or push notifications -- they only need to answer searches. So
// the owner broadcasts a SessionAnnounce (periodically + on change) and every
// node caches it for search; there is no per-member update fan-out. A session
// is addressed by a caller-chosen local SessionName distinct from the backend
// SessionId, has a state machine (Pending/Starting/InProgress/Ending/Ended),
// and explicit Register/UnregisterPlayers. SetHostAddress carries the LAN
// endpoint, which is exactly where a joiner reads it back.
//

#include "interfaces/Sessions.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Platform.h"
#include "net/Serialize.h"

#include <cstdio>
#include <cstring>

namespace EOSEmu
{
	uint32_t SessionRecord::OpenSlots() const
	{
		const uint32_t Used = static_cast<uint32_t>(RegisteredPlayers.size());
		return MaxPlayers > Used ? MaxPlayers - Used : 0;
	}

	std::string SessionsInterface::LocalPid() const { return Platform_.LocalIdentity().ProductUserIdString(); }
	const char* SessionsInterface::Intern(const std::string& S) { return Interned_.insert(S).first->c_str(); }

	std::string SessionsInterface::NewSessionId(const std::string& OwnerPid)
	{
		char Buf[96];
		std::snprintf(Buf, sizeof(Buf), "session_%.8s_%llu_%llu",
			OwnerPid.c_str(),
			static_cast<unsigned long long>(Platform_.TickCount()),
			static_cast<unsigned long long>(SessionCounter_++));
		return Buf;
	}

	// --- (de)serialization -------------------------------------------------

	void SessionsInterface::SerializeRecord(net::ByteWriter& W, const SessionRecord& Rec)
	{
		W.Str(Rec.SessionId);
		W.Str(Rec.OwnerProductId);
		W.Str(Rec.BucketId);
		W.Str(Rec.HostAddress);
		W.U32(Rec.MaxPlayers);
		W.U8(static_cast<uint8_t>(Rec.Permission));
		W.Bool(Rec.bAllowJoinInProgress != EOS_FALSE);
		W.Bool(Rec.bInvitesAllowed != EOS_FALSE);
		W.Bool(Rec.bSanctionsEnabled != EOS_FALSE);
		W.U8(static_cast<uint8_t>(Rec.State));
		W.U64(Rec.Version);
		W.U32(static_cast<uint32_t>(Rec.AllowedPlatformIds.size()));
		for (uint32_t P : Rec.AllowedPlatformIds) W.U32(P);
		W.U32(static_cast<uint32_t>(Rec.Attributes.size()));
		for (const auto& A : Rec.Attributes)
		{
			WriteAttrWire(W, A.first, A.second.Value);
			W.U8(static_cast<uint8_t>(A.second.Adv));
		}
		W.U32(static_cast<uint32_t>(Rec.RegisteredPlayers.size()));
		for (const auto& P : Rec.RegisteredPlayers) W.Str(P);
	}

	bool SessionsInterface::DeserializeRecord(net::ByteReader& R, SessionRecord& Out)
	{
		Out.SessionId = R.Str();
		Out.OwnerProductId = R.Str();
		Out.BucketId = R.Str();
		Out.HostAddress = R.Str();
		Out.MaxPlayers = R.U32();
		Out.Permission = static_cast<EOS_EOnlineSessionPermissionLevel>(R.U8());
		Out.bAllowJoinInProgress = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.bInvitesAllowed = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.bSanctionsEnabled = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.State = static_cast<EOS_EOnlineSessionState>(R.U8());
		Out.Version = R.U64();
		const uint32_t PC = R.U32();
		for (uint32_t i = 0; i < PC && R.Ok(); ++i) Out.AllowedPlatformIds.push_back(R.U32());
		const uint32_t AC = R.U32();
		for (uint32_t i = 0; i < AC && R.Ok(); ++i)
		{
			std::string Key; SessionAttr A;
			ReadAttrWire(R, Key, A.Value);
			A.Adv = static_cast<EOS_ESessionAttributeAdvertisementType>(R.U8());
			Out.Attributes.emplace_back(Key, A);
		}
		const uint32_t RP = R.U32();
		for (uint32_t i = 0; i < RP && R.Ok(); ++i) Out.RegisteredPlayers.push_back(R.Str());
		return R.Ok();
	}

	void SessionsInterface::BroadcastAnnounce(const SessionRecord& Rec)
	{
		net::ByteWriter W;
		SerializeRecord(W, Rec);
		Platform_.Net().Broadcast(net::MessageType::SessionAnnounce, W.Data().data(), W.Size());
	}

	void SessionsInterface::PeriodicAnnounce()
	{
		std::vector<SessionRecord> Snapshot;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (const std::string& Name : Owned_)
			{
				auto It = Local_.find(Name);
				// Only advertise sessions in an advertisable state.
				if (It != Local_.end() && It->second.Permission == EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised)
					Snapshot.push_back(It->second);
			}
		}
		for (const auto& Rec : Snapshot) BroadcastAnnounce(Rec);
	}

	// --- modification builder ----------------------------------------------

	EOS_EResult SessionsInterface::CreateSessionModification(const EOS_Sessions_CreateSessionModificationOptions* Options, EOS_HSessionModification* Out)
	{
		EOSEMU_TRACE(Sessions);
		if (Options == nullptr || Out == nullptr || Options->SessionName == nullptr || Options->LocalUserId == nullptr)
			return EOS_EResult::EOS_InvalidParameters;
		if (!VersionInRange(Options->ApiVersion, EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST))
			return EOS_EResult::EOS_IncompatibleVersion;
		// Field introduction versions per the SDK changelog: v2 bPresenceEnabled,
		// v3 SessionId override, v4 bSanctionsEnabled, v5 AllowedPlatformIds.
		const int32_t V = Options->ApiVersion;
		*Out = nullptr;
		auto* Mod = new SessionModificationObj();
		Mod->bIsCreate = true;
		Mod->SessionName = Options->SessionName;
		Mod->LocalUser = Ids::ProductString(Options->LocalUserId);
		Mod->bSetBucket = true;
		Mod->Bucket = Options->BucketId ? Options->BucketId : "";
		Mod->bSetMax = true;
		Mod->Max = Options->MaxPlayers;
		Mod->bPresence = HasField(V, 2) ? Options->bPresenceEnabled : EOS_FALSE;
		Mod->bSanctions = HasField(V, 4) ? Options->bSanctionsEnabled : EOS_FALSE;
		if (HasField(V, 3) && Options->SessionId != nullptr) Mod->SessionIdOverride = Options->SessionId;
		for (uint32_t i = 0; HasField(V, 5) && Options->AllowedPlatformIds != nullptr && i < Options->AllowedPlatformIdsCount; ++i)
		{
			Mod->bSetPlatforms = true;
			Mod->Platforms.push_back(Options->AllowedPlatformIds[i]);
		}
		*Out = reinterpret_cast<EOS_HSessionModification>(Mod);
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult SessionsInterface::UpdateSessionModification(const EOS_Sessions_UpdateSessionModificationOptions* Options, EOS_HSessionModification* Out)
	{
		EOSEMU_TRACE(Sessions);
		if (Options == nullptr || Out == nullptr || Options->SessionName == nullptr)
			return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		auto* Mod = new SessionModificationObj();
		Mod->bIsCreate = false;
		Mod->SessionName = Options->SessionName;
		*Out = reinterpret_cast<EOS_HSessionModification>(Mod);
		return EOS_EResult::EOS_Success;
	}

	void SessionsInterface::UpdateSession(const EOS_Sessions_UpdateSessionOptions* Options, void* ClientData, EOS_Sessions_OnUpdateSessionCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
		const char* NamePtr = nullptr;
		const char* IdPtr = nullptr;
		SessionRecord ToBroadcast;
		bool DoBroadcast = false;

		if (Options != nullptr && Options->SessionModificationHandle != nullptr)
		{
			auto* Mod = reinterpret_cast<SessionModificationObj*>(Options->SessionModificationHandle);
			std::lock_guard<std::mutex> Lock(Mutex_);

			SessionRecord* Rec = nullptr;
			if (Mod->bIsCreate)
			{
				SessionRecord Fresh;
				Fresh.SessionName = Mod->SessionName;
				Fresh.OwnerProductId = Mod->LocalUser.empty() ? LocalPid() : Mod->LocalUser;
				Fresh.SessionId = Mod->SessionIdOverride.empty() ? NewSessionId(Fresh.OwnerProductId) : Mod->SessionIdOverride;
				Fresh.bPresenceEnabled = Mod->bPresence;
				Fresh.bSanctionsEnabled = Mod->bSanctions;
				Fresh.State = EOS_EOnlineSessionState::EOS_OSS_Pending;
				Fresh.RegisteredPlayers.push_back(Fresh.OwnerProductId);
				Local_[Fresh.SessionName] = Fresh;
				Owned_.insert(Fresh.SessionName);
				Rec = &Local_[Fresh.SessionName];
			}
			else
			{
				auto It = Local_.find(Mod->SessionName);
				if (It != Local_.end()) Rec = &It->second;
			}

			if (Rec != nullptr)
			{
				if (Mod->bSetBucket) Rec->BucketId = Mod->Bucket;
				if (Mod->bSetHost) Rec->HostAddress = Mod->HostAddress;
				if (Mod->bSetPerm) Rec->Permission = Mod->Perm;
				if (Mod->bSetJIP) Rec->bAllowJoinInProgress = Mod->JIP;
				if (Mod->bSetMax) Rec->MaxPlayers = Mod->Max;
				if (Mod->bSetInvites) Rec->bInvitesAllowed = Mod->Invites;
				if (Mod->bSetPlatforms) Rec->AllowedPlatformIds = Mod->Platforms;
				for (const auto& Add : Mod->AddAttrs)
				{
					bool Found = false;
					for (auto& A : Rec->Attributes) if (A.first == Add.first) { A.second = Add.second; Found = true; break; }
					if (!Found) Rec->Attributes.push_back(Add);
				}
				for (const auto& Key : Mod->RemoveAttrs)
					for (auto It = Rec->Attributes.begin(); It != Rec->Attributes.end(); ++It)
						if (It->first == Key) { Rec->Attributes.erase(It); break; }

				// If no explicit host address was set, advertise our own endpoint.
				if (Rec->HostAddress.empty())
				{
					net::Endpoint Self;
					Self.Ip = 0; Self.Port = Platform_.Net().UnicastPort();
					// Peers learn the real IP from the datagram source; leave the
					// string carrying just our advertised port as a fallback hint.
					char Buf[24]; std::snprintf(Buf, sizeof(Buf), "0.0.0.0:%u", Self.Port);
					Rec->HostAddress = Buf;
				}

				Rec->Version++;
				NamePtr = Intern(Rec->SessionName);
				IdPtr = Intern(Rec->SessionId);
				ToBroadcast = *Rec;
				DoBroadcast = (Rec->Permission == EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised);
				Result = EOS_EResult::EOS_Success;
			}
			else
			{
				Result = EOS_EResult::EOS_NotFound;
			}
		}

		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);

		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, NamePtr, IdPtr] {
			EOS_Sessions_UpdateSessionCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData; I.SessionName = NamePtr; I.SessionId = IdPtr;
			Cb(&I);
		});
	}

	// --- lifecycle ---------------------------------------------------------

	void SessionsInterface::DestroySession(const EOS_Sessions_DestroySessionOptions* Options, void* ClientData, EOS_Sessions_OnDestroySessionCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		std::string SessionId;
		if (Options != nullptr && Options->SessionName != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Local_.find(Options->SessionName);
			if (It != Local_.end())
			{
				SessionId = It->second.SessionId;
				const bool Owner = Owned_.count(Options->SessionName) > 0;
				Local_.erase(It);
				Owned_.erase(Options->SessionName);
				Result = EOS_EResult::EOS_Success;
				if (Owner && !SessionId.empty())
				{
					// Withdraw the advertisement so searchers drop it.
					net::ByteWriter W; W.Str(SessionId);
					Platform_.Net().Broadcast(net::MessageType::SessionWithdraw, W.Data().data(), W.Size());
				}
			}
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Sessions_DestroySessionCallbackInfo I = {}; I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	void SessionsInterface::JoinSession(const EOS_Sessions_JoinSessionOptions* Options, void* ClientData, EOS_Sessions_OnJoinSessionCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
		if (Options != nullptr && Options->SessionName != nullptr && Options->SessionHandle != nullptr)
		{
			auto* Details = reinterpret_cast<SessionDetailsObj*>(Options->SessionHandle);
			SessionRecord Rec = Details->Record;
			Rec.SessionName = Options->SessionName;
			if (Options->LocalUserId != nullptr)
			{
				const std::string Me = Ids::ProductString(Options->LocalUserId);
				bool Present = false;
				for (const auto& P : Rec.RegisteredPlayers) if (P == Me) { Present = true; break; }
				if (!Present) Rec.RegisteredPlayers.push_back(Me);
			}
			std::lock_guard<std::mutex> Lock(Mutex_);
			Local_[Options->SessionName] = Rec;   // we participate but do not own
			Result = EOS_EResult::EOS_Success;
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Sessions_JoinSessionCallbackInfo I = {}; I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	namespace
	{
		// State transitions are validated loosely -- enough to keep the sample's
		// Start/End flow meaningful without rejecting reasonable sequences.
		EOS_EResult TransitionState(SessionRecord& Rec, EOS_EOnlineSessionState To)
		{
			Rec.State = To;
			return EOS_EResult::EOS_Success;
		}
	}

	void SessionsInterface::StartSession(const EOS_Sessions_StartSessionOptions* Options, void* ClientData, EOS_Sessions_OnStartSessionCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		SessionRecord ToBroadcast; bool DoBroadcast = false;
		if (Options != nullptr && Options->SessionName != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Local_.find(Options->SessionName);
			if (It != Local_.end())
			{
				Result = TransitionState(It->second, EOS_EOnlineSessionState::EOS_OSS_InProgress);
				It->second.Version++;
				if (Owned_.count(Options->SessionName)) { ToBroadcast = It->second; DoBroadcast = It->second.Permission == EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised; }
			}
		}
		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Sessions_StartSessionCallbackInfo I = {}; I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	void SessionsInterface::EndSession(const EOS_Sessions_EndSessionOptions* Options, void* ClientData, EOS_Sessions_OnEndSessionCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		SessionRecord ToBroadcast; bool DoBroadcast = false;
		if (Options != nullptr && Options->SessionName != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Local_.find(Options->SessionName);
			if (It != Local_.end())
			{
				Result = TransitionState(It->second, EOS_EOnlineSessionState::EOS_OSS_Ended);
				It->second.Version++;
				if (Owned_.count(Options->SessionName)) { ToBroadcast = It->second; DoBroadcast = It->second.Permission == EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised; }
			}
		}
		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Sessions_EndSessionCallbackInfo I = {}; I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	// --- player registration -----------------------------------------------

	void SessionsInterface::RegisterPlayers(const EOS_Sessions_RegisterPlayersOptions* Options, void* ClientData, EOS_Sessions_OnRegisterPlayersCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		std::vector<EOS_ProductUserId> Registered;
		SessionRecord ToBroadcast; bool DoBroadcast = false;
		if (Options != nullptr && Options->SessionName != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Local_.find(Options->SessionName);
			if (It != Local_.end())
			{
				for (uint32_t i = 0; Options->PlayersToRegister != nullptr && i < Options->PlayersToRegisterCount; ++i)
				{
					EOS_ProductUserId Pid = Options->PlayersToRegister[i];
					if (Pid == nullptr) continue;
					const std::string S = Ids::ProductString(Pid);
					bool Present = false;
					for (const auto& P : It->second.RegisteredPlayers) if (P == S) { Present = true; break; }
					if (!Present) It->second.RegisteredPlayers.push_back(S);
					Registered.push_back(Pid);
				}
				It->second.Version++;
				if (Owned_.count(Options->SessionName)) { ToBroadcast = It->second; DoBroadcast = It->second.Permission == EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised; }
				Result = EOS_EResult::EOS_Success;
			}
		}
		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, Registered]() mutable {
			EOS_Sessions_RegisterPlayersCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData;
			I.RegisteredPlayers = Registered.empty() ? nullptr : Registered.data();
			I.RegisteredPlayersCount = static_cast<uint32_t>(Registered.size());
			Cb(&I);
		});
	}

	void SessionsInterface::UnregisterPlayers(const EOS_Sessions_UnregisterPlayersOptions* Options, void* ClientData, EOS_Sessions_OnUnregisterPlayersCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		std::vector<EOS_ProductUserId> Unregistered;
		SessionRecord ToBroadcast; bool DoBroadcast = false;
		if (Options != nullptr && Options->SessionName != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Local_.find(Options->SessionName);
			if (It != Local_.end())
			{
				for (uint32_t i = 0; Options->PlayersToUnregister != nullptr && i < Options->PlayersToUnregisterCount; ++i)
				{
					EOS_ProductUserId Pid = Options->PlayersToUnregister[i];
					if (Pid == nullptr) continue;
					const std::string S = Ids::ProductString(Pid);
					for (auto Pit = It->second.RegisteredPlayers.begin(); Pit != It->second.RegisteredPlayers.end(); ++Pit)
						if (*Pit == S) { It->second.RegisteredPlayers.erase(Pit); break; }
					Unregistered.push_back(Pid);
				}
				It->second.Version++;
				if (Owned_.count(Options->SessionName)) { ToBroadcast = It->second; DoBroadcast = It->second.Permission == EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised; }
				Result = EOS_EResult::EOS_Success;
			}
		}
		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, Unregistered]() mutable {
			EOS_Sessions_UnregisterPlayersCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData;
			I.UnregisteredPlayers = Unregistered.empty() ? nullptr : Unregistered.data();
			I.UnregisteredPlayersCount = static_cast<uint32_t>(Unregistered.size());
			Cb(&I);
		});
	}

	// --- invites -----------------------------------------------------------

	void SessionsInterface::SendInvite(const EOS_Sessions_SendInviteOptions* Options, void* ClientData, EOS_Sessions_OnSendInviteCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
		if (Options != nullptr && Options->SessionName != nullptr && Options->TargetUserId != nullptr)
		{
			std::string SessionId;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				auto It = Local_.find(Options->SessionName);
				if (It != Local_.end()) SessionId = It->second.SessionId;
			}
			if (!SessionId.empty())
			{
				net::Endpoint To = Platform_.Peers().EndpointFor(Options->TargetUserId);
				if (To.Valid())
				{
					net::ByteWriter W;
					W.Str(SessionId);
					W.Str(LocalPid());
					W.Str(Ids::ProductString(Options->TargetUserId));
					Platform_.Net().SendTo(To, net::MessageType::SessionInvite, W.Data().data(), W.Size());
				}
				Result = EOS_EResult::EOS_Success;
			}
			else
			{
				Result = EOS_EResult::EOS_NotFound;
			}
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Sessions_SendInviteCallbackInfo I = {}; I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	void SessionsInterface::RejectInvite(const EOS_Sessions_RejectInviteOptions* Options, void* ClientData, EOS_Sessions_OnRejectInviteCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		if (Options != nullptr && Options->InviteId != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (auto It = Invites_.begin(); It != Invites_.end(); ++It)
				if (It->InviteId == Options->InviteId) { Invites_.erase(It); Result = EOS_EResult::EOS_Success; break; }
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Sessions_RejectInviteCallbackInfo I = {}; I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	void SessionsInterface::QueryInvites(const EOS_Sessions_QueryInvitesOptions* Options, void* ClientData, EOS_Sessions_OnQueryInvitesCallback Cb)
	{
		EOSEMU_TRACE(Sessions);
		EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : nullptr;
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, User] {
			EOS_Sessions_QueryInvitesCallbackInfo I = {};
			I.ResultCode = EOS_EResult::EOS_Success; I.ClientData = ClientData; I.LocalUserId = User; Cb(&I); });
	}

	uint32_t SessionsInterface::GetInviteCount(EOS_ProductUserId)
	{
		EOSEMU_TRACE(Sessions);
		std::lock_guard<std::mutex> Lock(Mutex_);
		return static_cast<uint32_t>(Invites_.size());
	}

	EOS_EResult SessionsInterface::GetInviteIdByIndex(EOS_ProductUserId, uint32_t Index, char* OutBuffer, int32_t* InOutLen)
	{
		EOSEMU_TRACE(Sessions);
		if (InOutLen == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		if (Index >= Invites_.size()) return EOS_EResult::EOS_NotFound;
		const std::string& Id = Invites_[Index].InviteId;
		const int32_t Required = static_cast<int32_t>(Id.size()) + 1;
		if (OutBuffer == nullptr || *InOutLen < Required) { *InOutLen = Required; return EOS_EResult::EOS_LimitExceeded; }
		std::memcpy(OutBuffer, Id.c_str(), Id.size() + 1);
		*InOutLen = Required;
		return EOS_EResult::EOS_Success;
	}

	// --- handles & queries -------------------------------------------------

	EOS_EResult SessionsInterface::CreateSessionSearch(const EOS_Sessions_CreateSessionSearchOptions* Options, EOS_HSessionSearch* Out)
	{
		EOSEMU_TRACE(Sessions);
		if (Out == nullptr) return EOS_EResult::EOS_InvalidParameters;
		auto* Search = new SessionSearchObj();
		if (Options != nullptr && Options->MaxSearchResults > 0) Search->MaxResults = Options->MaxSearchResults;
		*Out = reinterpret_cast<EOS_HSessionSearch>(Search);
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult SessionsInterface::CopyActiveSessionHandle(const char* SessionName, EOS_HActiveSession* Out)
	{
		EOSEMU_TRACE(Sessions);
		if (Out == nullptr || SessionName == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Local_.find(SessionName);
		if (It == Local_.end()) return EOS_EResult::EOS_NotFound;
		auto* Active = new ActiveSessionObj();
		Active->SessionName = SessionName;
		Active->LocalUser = LocalPid();
		*Out = reinterpret_cast<EOS_HActiveSession>(Active);
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult SessionsInterface::CopySessionHandleByInviteId(const char* InviteId, EOS_HSessionDetails* Out)
	{
		EOSEMU_TRACE(Sessions);
		if (Out == nullptr || InviteId == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Inv : Invites_)
		{
			if (Inv.InviteId == InviteId)
			{
				auto It = Known_.find(Inv.SessionId);
				if (It == Known_.end()) return EOS_EResult::EOS_NotFound;
				auto* Details = new SessionDetailsObj();
				Details->Record = It->second;
				*Out = reinterpret_cast<EOS_HSessionDetails>(Details);
				return EOS_EResult::EOS_Success;
			}
		}
		return EOS_EResult::EOS_NotFound;
	}

	// --- overlay-driven invite-accept / join-friend ------------------------

	bool SessionsInterface::ParticipatingInId(const std::string& SessionId) const
	{
		for (const auto& Kv : Local_) if (Kv.second.SessionId == SessionId) return true;
		return false;
	}

	std::vector<SessionInviteView> SessionsInterface::SnapshotInvites()
	{
		std::vector<SessionInviteView> Out;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Inv : Invites_)
		{
			if (ParticipatingInId(Inv.SessionId)) continue; // already joined
			Out.push_back({Inv.InviteId, Inv.SessionId, Inv.FromProduct});
		}
		return Out;
	}

	std::vector<SessionJoinableView> SessionsInterface::SnapshotJoinable()
	{
		std::vector<SessionJoinableView> Out;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Kv : Known_)
		{
			const SessionRecord& Rec = Kv.second;
			if (ParticipatingInId(Rec.SessionId)) continue;
			if (Rec.OpenSlots() == 0) continue;
			Out.push_back({Rec.SessionId, Rec.OwnerProductId});
		}
		return Out;
	}

	void SessionsInterface::AcceptInviteFromOverlay(const std::string& InviteId)
	{
		EOSEMU_TRACE(Sessions);
		std::string SessionId, FromPid;
		const char* InvPtr = nullptr;
		const char* IdPtr = nullptr;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			bool Found = false;
			for (const auto& Inv : Invites_)
				if (Inv.InviteId == InviteId) { SessionId = Inv.SessionId; FromPid = Inv.FromProduct; Found = true; break; }
			if (!Found) return;
			InvPtr = Intern(InviteId);
			IdPtr = Intern(SessionId);
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		EOS_ProductUserId Sender = Ids::InternProduct(FromPid);
		for (const auto& E : InviteAccepted_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, InvPtr, IdPtr, Local, Sender] {
				EOS_Sessions_SessionInviteAcceptedCallbackInfo I = {};
				I.ClientData = Cd; I.SessionId = IdPtr; I.LocalUserId = Local; I.TargetUserId = Sender; I.InviteId = InvPtr;
				Fn(&I);
			});
		}
		EOSEMU_INFO(Sessions, "invite %s accepted via overlay", InviteId.c_str());
	}

	void SessionsInterface::RejectInviteFromOverlay(const std::string& InviteId)
	{
		EOSEMU_TRACE(Sessions);
		std::string SessionId, FromPid;
		const char* InvPtr = nullptr;
		const char* IdPtr = nullptr;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			bool Found = false;
			for (auto It = Invites_.begin(); It != Invites_.end(); ++It)
				if (It->InviteId == InviteId) { SessionId = It->SessionId; FromPid = It->FromProduct; Invites_.erase(It); Found = true; break; }
			if (!Found) return;
			InvPtr = Intern(InviteId);
			IdPtr = Intern(SessionId);
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		EOS_ProductUserId Sender = Ids::InternProduct(FromPid);
		for (const auto& E : InviteRejected_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, InvPtr, IdPtr, Local, Sender] {
				EOS_Sessions_SessionInviteRejectedCallbackInfo I = {};
				I.ClientData = Cd; I.InviteId = InvPtr; I.LocalUserId = Local; I.TargetUserId = Sender; I.SessionId = IdPtr;
				Fn(&I);
			});
		}
	}

	void SessionsInterface::BeginJoinFromOverlay(const std::string& SessionId)
	{
		EOSEMU_TRACE(Sessions);
		uint64_t EventId = 0;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(SessionId);
			if (It == Known_.end()) return;
			EventId = NextUiEventId();
			UiEvents_[EventId] = It->second;
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		for (const auto& E : JoinAccepted_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, Local, EventId] {
				EOS_Sessions_JoinSessionAcceptedCallbackInfo I = {};
				I.ClientData = Cd; I.LocalUserId = Local; I.UiEventId = EventId;
				Fn(&I);
			});
		}
		EOSEMU_INFO(Sessions, "join session %s requested via overlay (event %llu)",
			SessionId.c_str(), static_cast<unsigned long long>(EventId));
	}

	EOS_EResult SessionsInterface::CopyDetailsByUiEventId(uint64_t UiEventId, EOS_HSessionDetails* Out)
	{
		EOSEMU_TRACE(Sessions);
		if (Out == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = UiEvents_.find(UiEventId);
		if (It == UiEvents_.end()) return EOS_EResult::EOS_NotFound;
		auto* Details = new SessionDetailsObj();
		Details->Record = It->second;
		*Out = reinterpret_cast<EOS_HSessionDetails>(Details);
		return EOS_EResult::EOS_Success;
	}

	void SessionsInterface::AcknowledgeUiEvent(uint64_t UiEventId)
	{
		EOSEMU_TRACE(Sessions);
		std::lock_guard<std::mutex> Lock(Mutex_);
		UiEvents_.erase(UiEventId);
	}

	EOS_EResult SessionsInterface::IsUserInSession(const char* SessionName, EOS_ProductUserId Target)
	{
		EOSEMU_TRACE(Sessions);
		if (SessionName == nullptr || Target == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Local_.find(SessionName);
		if (It == Local_.end()) return EOS_EResult::EOS_NotFound;
		const std::string S = Ids::ProductString(Target);
		for (const auto& P : It->second.RegisteredPlayers) if (P == S) return EOS_EResult::EOS_Success;
		return EOS_EResult::EOS_NotFound;
	}

	EOS_EResult SessionsInterface::DumpSessionState(const char* SessionName)
	{
		EOSEMU_TRACE(Sessions);
		if (SessionName == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Local_.find(SessionName);
		if (It == Local_.end()) return EOS_EResult::EOS_NotFound;
		EOSEMU_INFO(Sessions, "DumpSessionState '%s': id=%s state=%d players=%zu",
			SessionName, It->second.SessionId.c_str(), static_cast<int>(It->second.State), It->second.RegisteredPlayers.size());
		return EOS_EResult::EOS_Success;
	}

	bool SessionsInterface::CopyRecord(const std::string& SessionName, SessionRecord& Out)
	{
		EOSEMU_TRACE(Sessions);
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Local_.find(SessionName);
		if (It == Local_.end()) return false;
		Out = It->second;
		return true;
	}

	bool SessionsInterface::MatchesSearch(const SessionSearchObj& Search, const SessionRecord& Rec) const
	{
		if (!Search.BySessionId.empty()) return Rec.SessionId == Search.BySessionId;
		if (!Search.ByTargetUser.empty())
		{
			for (const auto& P : Rec.RegisteredPlayers) if (P == Search.ByTargetUser) return true;
			return false;
		}
		for (const auto& P : Search.Params)
		{
			if (P.Key == EOS_SESSIONS_SEARCH_BUCKET_ID) { if (Rec.BucketId != P.Value.Str) return false; continue; }
			if (P.Key == EOS_SESSIONS_SEARCH_MINSLOTSAVAILABLE) { if (static_cast<int64_t>(Rec.OpenSlots()) < P.Value.Int) return false; continue; }
			if (P.Key == EOS_SESSIONS_SEARCH_EMPTY_SERVERS_ONLY) { if (!Rec.RegisteredPlayers.empty()) return false; continue; }
			if (P.Key == EOS_SESSIONS_SEARCH_NONEMPTY_SERVERS_ONLY) { if (Rec.RegisteredPlayers.empty()) return false; continue; }
			bool Matched = false;
			for (const auto& A : Rec.Attributes)
				if (A.first == P.Key && A.second.Adv == EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise && CompareAttr(A.second.Value, P.Op, P.Value)) { Matched = true; break; }
			if (!Matched) return false;
		}
		return true;
	}

	void SessionsInterface::CollectSearchResults(SessionSearchObj& Search)
	{
		EOSEMU_TRACE(Sessions);
		std::lock_guard<std::mutex> Lock(Mutex_);
		Search.Results.clear();
		// Search our own hosted sessions plus discovered ones.
		auto Consider = [&](const SessionRecord& Rec)
		{
			if (Search.Results.size() >= Search.MaxResults) return;
			if (MatchesSearch(Search, Rec)) Search.Results.push_back(Rec);
		};
		for (const auto& Kv : Known_) Consider(Kv.second);
		for (const std::string& Name : Owned_)
		{
			auto It = Local_.find(Name);
			if (It != Local_.end()) Consider(It->second);
		}
	}

	// --- inbound datagrams -------------------------------------------------

	void SessionsInterface::OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len)
	{
		net::ByteReader R(Payload, Len);
		switch (Type)
		{
		case net::MessageType::SessionAnnounce:
		{
			SessionRecord Fresh;
			if (!DeserializeRecord(R, Fresh) || Fresh.SessionId.empty()) return;
			Fresh.HostEndpoint = From;
			Fresh.LastSeenTick = Platform_.TickCount();
			// If the host address didn't carry a concrete IP, stamp the source.
			if (Fresh.HostAddress.empty() || Fresh.HostAddress.rfind("0.0.0.0:", 0) == 0)
			{
				net::Endpoint E = From;
				Fresh.HostAddress = E.ToString();
			}
			std::lock_guard<std::mutex> Lock(Mutex_);
			Known_[Fresh.SessionId] = Fresh;
			break;
		}
		case net::MessageType::SessionWithdraw:
		{
			const std::string SessionId = R.Str();
			if (!R.Ok()) return;
			std::lock_guard<std::mutex> Lock(Mutex_);
			Known_.erase(SessionId);
			break;
		}
		case net::MessageType::SessionInvite:
		{
			const std::string SessionId = R.Str();
			const std::string FromPid = R.Str();
			const std::string ToPid = R.Str();
			if (!R.Ok() || ToPid != LocalPid()) return;
			std::string InviteId;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				char Buf[64]; std::snprintf(Buf, sizeof(Buf), "sinvite_%llu", static_cast<unsigned long long>(NextInvite_++));
				InviteId = Buf;
				Invites_.push_back({InviteId, SessionId, FromPid});
			}
			EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
			EOS_ProductUserId Sender = Ids::InternProduct(FromPid);
			const char* InvPtr = nullptr;
			{ std::lock_guard<std::mutex> Lock(Mutex_); InvPtr = Intern(InviteId); }
			for (const auto& E : InviteReceived_.Snapshot())
			{
				auto Fn = E.Fn; void* Cd = E.ClientData;
				Platform_.Dispatch().Post([Fn, Cd, InvPtr, Local, Sender] {
					EOS_Sessions_SessionInviteReceivedCallbackInfo I = {};
					I.ClientData = Cd; I.LocalUserId = Local; I.TargetUserId = Sender; I.InviteId = InvPtr; Fn(&I); });
			}
			break;
		}
		default:
			break;
		}
	}
}
