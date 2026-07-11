//
// Lobby interface over LAN.
//
// State model: the owner holds the authoritative LobbyRecord and broadcasts a
// full LobbyAnnounce on every change and periodically (so late searchers and
// members converge). Every node caches announcements in Known_ -- members use
// them to fire LobbyUpdate/MemberUpdate/MemberStatus notifications, searchers
// use them to answer Find. Join is optimistic: the joiner adds itself locally
// and sends a JoinRequest so the owner reconciles and re-announces.
//
// The two-phase builder (UpdateLobbyModification -> mutators -> UpdateLobby)
// mirrors the header. CreateLobby builds a record directly. See CLAUDE.md,
// "Lobby needs real state replication".
//

#include "interfaces/Lobby.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/Platform.h"
#include "net/Serialize.h"

#include <cstdio>
#include <cstring>

namespace EOSEmu
{
	// --- record helpers ----------------------------------------------------

	LobbyMemberRec* LobbyRecord::FindMember(const std::string& Pid)
	{
		for (auto& M : Members) if (M.ProductId == Pid) return &M;
		return nullptr;
	}
	const LobbyMemberRec* LobbyRecord::FindMember(const std::string& Pid) const
	{
		for (auto& M : Members) if (M.ProductId == Pid) return &M;
		return nullptr;
	}
	uint32_t LobbyRecord::AvailableSlots() const
	{
		const uint32_t Used = static_cast<uint32_t>(Members.size());
		return MaxMembers > Used ? MaxMembers - Used : 0;
	}

	namespace
	{
		using AttrList = std::vector<std::pair<std::string, LobbyAttr>>;

		void ApplyMemberAttrs(LobbyMemberRec& M, const AttrList& Adds, const std::vector<std::string>& Removes)
		{
			for (const auto& Add : Adds)
			{
				bool Found = false;
				for (auto& A : M.Attributes) if (A.first == Add.first) { A.second = Add.second; Found = true; break; }
				if (!Found) M.Attributes.push_back(Add);
			}
			for (const auto& Key : Removes)
			{
				for (auto It = M.Attributes.begin(); It != M.Attributes.end(); ++It)
					if (It->first == Key) { M.Attributes.erase(It); break; }
			}
		}

		// Order-insensitive: an in-place value update keeps order, but a
		// remove+re-add changes it and is still the "same attributes".
		bool MemberAttrsEqual(const AttrList& A, const AttrList& B)
		{
			if (A.size() != B.size()) return false;
			for (const auto& L : A)
			{
				bool Match = false;
				for (const auto& R : B)
				{
					if (L.first != R.first) continue;
					Match = AttrEqual(L.second.Value, R.second.Value) && L.second.Visibility == R.second.Visibility;
					break;
				}
				if (!Match) return false;
			}
			return true;
		}
	}

	std::string LobbyInterface::LocalPid() const
	{
		return Platform_.LocalIdentity().ProductUserIdString();
	}

	const char* LobbyInterface::Intern(const std::string& S)
	{
		return Interned_.insert(S).first->c_str();
	}

	std::string LobbyInterface::NewLobbyId(const std::string& OwnerPid)
	{
		char Buf[96];
		std::snprintf(Buf, sizeof(Buf), "lobby_%.8s_%llu_%llu",
			OwnerPid.c_str(),
			static_cast<unsigned long long>(Platform_.TickCount()),
			static_cast<unsigned long long>(LobbyCounter_++));
		return Buf;
	}

	// --- (de)serialization -------------------------------------------------

	void LobbyInterface::SerializeRecord(net::ByteWriter& W, const LobbyRecord& Rec)
	{
		W.Str(Rec.LobbyId);
		W.Str(Rec.OwnerProductId);
		W.Str(Rec.BucketId);
		W.U32(Rec.MaxMembers);
		W.U8(static_cast<uint8_t>(Rec.Permission));
		W.Bool(Rec.bAllowInvites != EOS_FALSE);
		W.Bool(Rec.bAllowHostMigration != EOS_FALSE);
		W.Bool(Rec.bRTCRoomEnabled != EOS_FALSE);
		W.Bool(Rec.bPresenceEnabled != EOS_FALSE);
		W.Bool(Rec.bAllowJoinById != EOS_FALSE);
		W.U64(Rec.Version);
		W.U32(static_cast<uint32_t>(Rec.AllowedPlatformIds.size()));
		for (uint32_t P : Rec.AllowedPlatformIds) W.U32(P);
		W.U32(static_cast<uint32_t>(Rec.Attributes.size()));
		for (const auto& A : Rec.Attributes)
		{
			WriteAttrWire(W, A.first, A.second.Value);
			W.U8(static_cast<uint8_t>(A.second.Visibility));
		}
		W.U32(static_cast<uint32_t>(Rec.Members.size()));
		for (const auto& M : Rec.Members)
		{
			W.Str(M.ProductId);
			W.U32(M.Platform);
			W.U32(static_cast<uint32_t>(M.Attributes.size()));
			for (const auto& A : M.Attributes)
			{
				WriteAttrWire(W, A.first, A.second.Value);
				W.U8(static_cast<uint8_t>(A.second.Visibility));
			}
		}
	}

	bool LobbyInterface::DeserializeRecord(net::ByteReader& R, LobbyRecord& Out)
	{
		Out.LobbyId = R.Str();
		Out.OwnerProductId = R.Str();
		Out.BucketId = R.Str();
		Out.MaxMembers = R.U32();
		Out.Permission = static_cast<EOS_ELobbyPermissionLevel>(R.U8());
		Out.bAllowInvites = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.bAllowHostMigration = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.bRTCRoomEnabled = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.bPresenceEnabled = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.bAllowJoinById = R.Bool() ? EOS_TRUE : EOS_FALSE;
		Out.Version = R.U64();
		const uint32_t PlatformCount = R.U32();
		for (uint32_t i = 0; i < PlatformCount && R.Ok(); ++i) Out.AllowedPlatformIds.push_back(R.U32());
		const uint32_t AttrCount = R.U32();
		for (uint32_t i = 0; i < AttrCount && R.Ok(); ++i)
		{
			std::string Key; LobbyAttr A;
			ReadAttrWire(R, Key, A.Value);
			A.Visibility = static_cast<EOS_ELobbyAttributeVisibility>(R.U8());
			Out.Attributes.emplace_back(Key, A);
		}
		const uint32_t MemberCount = R.U32();
		for (uint32_t i = 0; i < MemberCount && R.Ok(); ++i)
		{
			LobbyMemberRec M;
			M.ProductId = R.Str();
			M.Platform = R.U32();
			const uint32_t MA = R.U32();
			for (uint32_t j = 0; j < MA && R.Ok(); ++j)
			{
				std::string Key; LobbyAttr A;
				ReadAttrWire(R, Key, A.Value);
				A.Visibility = static_cast<EOS_ELobbyAttributeVisibility>(R.U8());
				M.Attributes.emplace_back(Key, A);
			}
			Out.Members.push_back(std::move(M));
		}
		return R.Ok();
	}

	void LobbyInterface::BroadcastAnnounce(const LobbyRecord& Rec)
	{
		net::ByteWriter W;
		SerializeRecord(W, Rec);
		Platform_.Net().Broadcast(net::MessageType::LobbyAnnounce, W.Data().data(), W.Size());
	}

	void LobbyInterface::PeriodicAnnounce()
	{
		std::vector<LobbyRecord> Snapshot;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (const std::string& Id : Owned_)
			{
				auto It = Known_.find(Id);
				if (It != Known_.end()) Snapshot.push_back(It->second);
			}
		}
		for (const auto& Rec : Snapshot) BroadcastAnnounce(Rec);
	}

	// --- create / update ---------------------------------------------------

	void LobbyInterface::CreateLobby(const EOS_Lobby_CreateLobbyOptions* Options, void* ClientData, EOS_Lobby_OnCreateLobbyCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		if (Options == nullptr || Options->LocalUserId == nullptr)
		{
			if (Cb) Platform_.Dispatch().Post([Cb, ClientData] {
				EOS_Lobby_CreateLobbyCallbackInfo I = {}; I.ResultCode = EOS_EResult::EOS_InvalidParameters; I.ClientData = ClientData; Cb(&I); });
			return;
		}
		if (!VersionInRange(Options->ApiVersion, EOS_LOBBY_CREATELOBBY_API_LATEST))
		{
			if (Cb) Platform_.Dispatch().Post([Cb, ClientData] {
				EOS_Lobby_CreateLobbyCallbackInfo I = {}; I.ResultCode = EOS_EResult::EOS_IncompatibleVersion; I.ClientData = ClientData; Cb(&I); });
			return;
		}

		// Field introduction versions per the SDK changelog: v2 bPresenceEnabled/
		// bAllowInvites, v3 BucketId, v4 bDisableHostMigration, v5 bEnableRTCRoom
		// (v6+ fields -- LocalRTCOptions, LobbyId override, bEnableJoinById,
		// bRejoinAfterKickRequiresInvite, AllowedPlatformIds -- are not read here).
		const int32_t V = Options->ApiVersion;
		LobbyRecord Rec;
		Rec.OwnerProductId = Ids::ProductString(Options->LocalUserId);
		Rec.MaxMembers = Options->MaxLobbyMembers;
		Rec.Permission = Options->PermissionLevel;
		Rec.bAllowInvites = HasField(V, 2) ? Options->bAllowInvites : EOS_TRUE;
		Rec.bPresenceEnabled = HasField(V, 2) ? Options->bPresenceEnabled : EOS_FALSE;
		Rec.bAllowHostMigration = (HasField(V, 4) && Options->bDisableHostMigration) ? EOS_FALSE : EOS_TRUE;
		Rec.bRTCRoomEnabled = HasField(V, 5) ? Options->bEnableRTCRoom : EOS_FALSE;
		Rec.BucketId = (HasField(V, 3) && Options->BucketId) ? Options->BucketId : "";
		Rec.Version = 1;

		LobbyMemberRec Self;
		Self.ProductId = Rec.OwnerProductId;
		Rec.Members.push_back(Self);

		const char* IdPtr = nullptr;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Rec.LobbyId = NewLobbyId(Rec.OwnerProductId);
			IdPtr = Intern(Rec.LobbyId);
			Known_[Rec.LobbyId] = Rec;
			MemberOf_.insert(Rec.LobbyId);
			Owned_.insert(Rec.LobbyId);
		}
		BroadcastAnnounce(Rec);
		EOSEMU_INFO(Lobby, "CreateLobby -> %s", Rec.LobbyId.c_str());

		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, IdPtr] {
			EOS_Lobby_CreateLobbyCallbackInfo I = {};
			I.ResultCode = EOS_EResult::EOS_Success;
			I.ClientData = ClientData;
			I.LobbyId = IdPtr;
			Cb(&I);
		});
	}

	EOS_EResult LobbyInterface::UpdateLobbyModification(const EOS_Lobby_UpdateLobbyModificationOptions* Options, EOS_HLobbyModification* Out)
	{
		EOSEMU_TRACE(Lobby);
		if (Options == nullptr || Out == nullptr || Options->LobbyId == nullptr || Options->LocalUserId == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		*Out = nullptr;
		auto* Mod = new LobbyModificationObj();
		Mod->LobbyId = Options->LobbyId;
		Mod->LocalUser = Ids::ProductString(Options->LocalUserId);
		*Out = reinterpret_cast<EOS_HLobbyModification>(Mod);
		return EOS_EResult::EOS_Success;
	}

	void LobbyInterface::ApplyModification(LobbyRecord& Rec, const LobbyModificationObj& Mod)
	{
		if (Mod.bSetBucket) Rec.BucketId = Mod.Bucket;
		if (Mod.bSetPerm) Rec.Permission = Mod.Perm;
		if (Mod.bSetMax) Rec.MaxMembers = Mod.Max;
		if (Mod.bSetInvites) Rec.bAllowInvites = Mod.Invites;
		if (Mod.bSetPlatforms) Rec.AllowedPlatformIds = Mod.Platforms;

		for (const auto& Add : Mod.AddAttrs)
		{
			bool Found = false;
			for (auto& A : Rec.Attributes) if (A.first == Add.first) { A.second = Add.second; Found = true; break; }
			if (!Found) Rec.Attributes.push_back(Add);
		}
		for (const auto& Key : Mod.RemoveAttrs)
		{
			for (auto It = Rec.Attributes.begin(); It != Rec.Attributes.end(); ++It)
				if (It->first == Key) { Rec.Attributes.erase(It); break; }
		}
		if (!Mod.AddMemberAttrs.empty() || !Mod.RemoveMemberAttrs.empty())
		{
			if (LobbyMemberRec* Me = Rec.FindMember(Mod.LocalUser))
			{
				ApplyMemberAttrs(*Me, Mod.AddMemberAttrs, Mod.RemoveMemberAttrs);
			}
		}
	}

	void LobbyInterface::UpdateLobby(const EOS_Lobby_UpdateLobbyOptions* Options, void* ClientData, EOS_Lobby_OnUpdateLobbyCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
		const char* IdPtr = nullptr;
		LobbyRecord ToBroadcast;
		bool DoBroadcast = false;
		net::Endpoint OwnerEndpoint;
		std::vector<uint8_t> OwnerPayload;

		if (Options != nullptr && Options->LobbyModificationHandle != nullptr)
		{
			auto* Mod = reinterpret_cast<LobbyModificationObj*>(Options->LobbyModificationHandle);
			const bool HasLobbyScope = Mod->bSetBucket || Mod->bSetPerm || Mod->bSetMax || Mod->bSetInvites
				|| Mod->bSetPlatforms || !Mod->AddAttrs.empty() || !Mod->RemoveAttrs.empty();

			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(Mod->LobbyId);
			if (It == Known_.end())
			{
				Result = EOS_EResult::EOS_NotFound;
			}
			else if (Owned_.count(Mod->LobbyId))
			{
				ApplyModification(It->second, *Mod);
				It->second.Version++;
				IdPtr = Intern(It->second.LobbyId);
				ToBroadcast = It->second;
				DoBroadcast = true;
				Result = EOS_EResult::EOS_Success;
			}
			else if (HasLobbyScope)
			{
				// Lobby-level attributes and settings are owner-only (eos_lobby.h).
				Result = EOS_EResult::EOS_Lobby_NotOwner;
			}
			else if (MemberOf_.count(Mod->LobbyId))
			{
				// Member-level change from a non-owner. Apply locally for
				// immediate read-back, then forward to the owner, which merges
				// it, bumps the version and re-announces. Without the forward
				// the owner's next periodic announce reverts the change on
				// every node (the owner ignores announces for owned lobbies).
				if (LobbyMemberRec* Me = It->second.FindMember(Mod->LocalUser))
				{
					ApplyMemberAttrs(*Me, Mod->AddMemberAttrs, Mod->RemoveMemberAttrs);
				}
				IdPtr = Intern(It->second.LobbyId);
				OwnerEndpoint = It->second.HostEndpoint;
				net::ByteWriter W;
				W.Str(Mod->LobbyId);
				W.Str(Mod->LocalUser);
				W.U32(static_cast<uint32_t>(Mod->AddMemberAttrs.size()));
				for (const auto& A : Mod->AddMemberAttrs)
				{
					WriteAttrWire(W, A.first, A.second.Value);
					W.U8(static_cast<uint8_t>(A.second.Visibility));
				}
				W.U32(static_cast<uint32_t>(Mod->RemoveMemberAttrs.size()));
				for (const auto& K : Mod->RemoveMemberAttrs) W.Str(K);
				OwnerPayload = W.Data();
				Result = EOS_EResult::EOS_Success;
			}
			else
			{
				Result = EOS_EResult::EOS_NotFound;
			}
		}

		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
		if (!OwnerPayload.empty() && OwnerEndpoint.Valid())
		{
			Platform_.Net().SendTo(OwnerEndpoint, net::MessageType::LobbyUpdate,
				OwnerPayload.data(), static_cast<uint16_t>(OwnerPayload.size()));
		}

		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_UpdateLobbyCallbackInfo I = {};
			I.ResultCode = Result;
			I.ClientData = ClientData;
			I.LobbyId = IdPtr;
			Cb(&I);
		});
	}

	// --- destroy / join / leave --------------------------------------------

	void LobbyInterface::DestroyLobby(const EOS_Lobby_DestroyLobbyOptions* Options, void* ClientData, EOS_Lobby_OnDestroyLobbyCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		const char* IdPtr = nullptr;
		std::string LobbyId;
		if (Options != nullptr && Options->LobbyId != nullptr)
		{
			LobbyId = Options->LobbyId;
			std::lock_guard<std::mutex> Lock(Mutex_);
			if (Owned_.count(LobbyId))
			{
				IdPtr = Intern(LobbyId);
				Known_.erase(LobbyId);
				MemberOf_.erase(LobbyId);
				Owned_.erase(LobbyId);
				Result = EOS_EResult::EOS_Success;
			}
		}
		if (Result == EOS_EResult::EOS_Success)
		{
			// Tell everyone the lobby is gone (subject = owner, reason = closed).
			net::ByteWriter W;
			W.Str(LobbyId);
			W.Str(LocalPid());
			W.U8(static_cast<uint8_t>(EOS_ELobbyMemberStatus::EOS_LMS_CLOSED));
			Platform_.Net().Broadcast(net::MessageType::LobbyLeave, W.Data().data(), W.Size());
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_DestroyLobbyCallbackInfo I = {};
			I.ResultCode = Result;
			I.ClientData = ClientData;
			I.LobbyId = IdPtr;
			Cb(&I);
		});
	}

	void LobbyInterface::JoinLobby(const EOS_Lobby_JoinLobbyOptions* Options, void* ClientData, EOS_Lobby_OnJoinLobbyCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
		const char* IdPtr = nullptr;
		net::Endpoint Host;
		std::string LobbyId;

		if (Options != nullptr && Options->LobbyDetailsHandle != nullptr && Options->LocalUserId != nullptr)
		{
			auto* Details = reinterpret_cast<LobbyDetailsObj*>(Options->LobbyDetailsHandle);
			LobbyRecord Rec = Details->Record;
			LobbyId = Rec.LobbyId;
			Host = Rec.HostEndpoint;
			const std::string Me = Ids::ProductString(Options->LocalUserId);

			std::lock_guard<std::mutex> Lock(Mutex_);
			LobbyRecord& Stored = Known_[LobbyId];
			if (Stored.LobbyId.empty()) Stored = Rec;      // first time we cache it
			if (!Stored.FindMember(Me))
			{
				LobbyMemberRec M; M.ProductId = Me;
				Stored.Members.push_back(M);
			}
			MemberOf_.insert(LobbyId);
			IdPtr = Intern(LobbyId);
			Result = EOS_EResult::EOS_Success;
		}

		// Ask the owner to add us so it re-announces the corrected membership.
		if (Result == EOS_EResult::EOS_Success && Host.Valid())
		{
			net::ByteWriter W;
			W.Str(LobbyId);
			W.Str(LocalPid());
			Platform_.Net().SendTo(Host, net::MessageType::LobbyJoinRequest, W.Data().data(), W.Size());
		}

		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_JoinLobbyCallbackInfo I = {};
			I.ResultCode = Result;
			I.ClientData = ClientData;
			I.LobbyId = IdPtr;
			Cb(&I);
		});
	}

	void LobbyInterface::JoinLobbyById(const EOS_Lobby_JoinLobbyByIdOptions* Options, void* ClientData, EOS_Lobby_OnJoinLobbyByIdCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		const char* IdPtr = nullptr;
		net::Endpoint Host;
		std::string LobbyId;
		if (Options != nullptr && Options->LobbyId != nullptr && Options->LocalUserId != nullptr)
		{
			LobbyId = Options->LobbyId;
			const std::string Me = Ids::ProductString(Options->LocalUserId);
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(LobbyId);
			if (It != Known_.end())
			{
				Host = It->second.HostEndpoint;
				if (!It->second.FindMember(Me))
				{
					LobbyMemberRec M; M.ProductId = Me;
					It->second.Members.push_back(M);
				}
				MemberOf_.insert(LobbyId);
				IdPtr = Intern(LobbyId);
				Result = EOS_EResult::EOS_Success;
			}
		}
		if (Result == EOS_EResult::EOS_Success && Host.Valid())
		{
			net::ByteWriter W;
			W.Str(LobbyId);
			W.Str(LocalPid());
			Platform_.Net().SendTo(Host, net::MessageType::LobbyJoinRequest, W.Data().data(), W.Size());
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_JoinLobbyByIdCallbackInfo I = {};
			I.ResultCode = Result;
			I.ClientData = ClientData;
			I.LobbyId = IdPtr;
			Cb(&I);
		});
	}

	void LobbyInterface::LeaveLobby(const EOS_Lobby_LeaveLobbyOptions* Options, void* ClientData, EOS_Lobby_OnLeaveLobbyCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		const char* IdPtr = nullptr;
		std::string LobbyId;
		bool WasOwner = false;
		LobbyRecord Reassigned;
		bool DoReannounce = false;

		if (Options != nullptr && Options->LobbyId != nullptr && Options->LocalUserId != nullptr)
		{
			LobbyId = Options->LobbyId;
			const std::string Me = Ids::ProductString(Options->LocalUserId);
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(LobbyId);
			if (It != Known_.end() && MemberOf_.count(LobbyId))
			{
				WasOwner = Owned_.count(LobbyId) > 0;
				LobbyRecord& Rec = It->second;
				for (auto Mit = Rec.Members.begin(); Mit != Rec.Members.end(); ++Mit)
					if (Mit->ProductId == Me) { Rec.Members.erase(Mit); break; }

				if (WasOwner && Rec.bAllowHostMigration && !Rec.Members.empty())
				{
					// Host migration: hand ownership to the next member.
					Rec.OwnerProductId = Rec.Members.front().ProductId;
					Rec.Version++;
					Reassigned = Rec;
					DoReannounce = true;  // this node still broadcasts the handoff once
				}
				IdPtr = Intern(LobbyId);
				MemberOf_.erase(LobbyId);
				Owned_.erase(LobbyId);
				if (!DoReannounce) Known_.erase(LobbyId);
				Result = EOS_EResult::EOS_Success;
			}
		}

		if (Result == EOS_EResult::EOS_Success)
		{
			net::ByteWriter W;
			W.Str(LobbyId);
			W.Str(Ids::ProductString(Options->LocalUserId));
			W.U8(static_cast<uint8_t>(EOS_ELobbyMemberStatus::EOS_LMS_LEFT));
			Platform_.Net().Broadcast(net::MessageType::LobbyLeave, W.Data().data(), W.Size());
			if (DoReannounce) BroadcastAnnounce(Reassigned);
		}

		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_LeaveLobbyCallbackInfo I = {};
			I.ResultCode = Result;
			I.ClientData = ClientData;
			I.LobbyId = IdPtr;
			Cb(&I);
		});
	}

	// --- member management -------------------------------------------------

	void LobbyInterface::PromoteMember(const EOS_Lobby_PromoteMemberOptions* Options, void* ClientData, EOS_Lobby_OnPromoteMemberCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		const char* IdPtr = nullptr;
		LobbyRecord ToBroadcast; bool DoBroadcast = false;
		if (Options != nullptr && Options->LobbyId != nullptr && Options->TargetUserId != nullptr)
		{
			const std::string Target = Ids::ProductString(Options->TargetUserId);
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(Options->LobbyId);
			if (It != Known_.end() && Owned_.count(It->first) && It->second.FindMember(Target))
			{
				It->second.OwnerProductId = Target;
				It->second.Version++;
				Owned_.erase(It->first); // we handed ownership away
				IdPtr = Intern(It->first);
				ToBroadcast = It->second; DoBroadcast = true;
				Result = EOS_EResult::EOS_Success;
			}
		}
		if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_PromoteMemberCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData; I.LobbyId = IdPtr; Cb(&I); });
	}

	void LobbyInterface::KickMember(const EOS_Lobby_KickMemberOptions* Options, void* ClientData, EOS_Lobby_OnKickMemberCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		const char* IdPtr = nullptr;
		std::string LobbyId, Target;
		LobbyRecord ToBroadcast; bool DoBroadcast = false;
		if (Options != nullptr && Options->LobbyId != nullptr && Options->TargetUserId != nullptr)
		{
			LobbyId = Options->LobbyId;
			Target = Ids::ProductString(Options->TargetUserId);
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(LobbyId);
			if (It != Known_.end() && Owned_.count(LobbyId))
			{
				LobbyRecord& Rec = It->second;
				for (auto Mit = Rec.Members.begin(); Mit != Rec.Members.end(); ++Mit)
					if (Mit->ProductId == Target) { Rec.Members.erase(Mit); break; }
				Rec.Version++;
				IdPtr = Intern(LobbyId);
				ToBroadcast = Rec; DoBroadcast = true;
				Result = EOS_EResult::EOS_Success;
			}
		}
		if (DoBroadcast)
		{
			net::ByteWriter W;
			W.Str(LobbyId); W.Str(Target); W.U8(static_cast<uint8_t>(EOS_ELobbyMemberStatus::EOS_LMS_KICKED));
			Platform_.Net().Broadcast(net::MessageType::LobbyLeave, W.Data().data(), W.Size());
			BroadcastAnnounce(ToBroadcast);
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_KickMemberCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData; I.LobbyId = IdPtr; Cb(&I); });
	}

	void LobbyInterface::HardMuteMember(const EOS_Lobby_HardMuteMemberOptions* Options, void* ClientData, EOS_Lobby_OnHardMuteMemberCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		// RTC is stubbed; accept the request so the caller's flow proceeds.
		const char* IdPtr = nullptr;
		if (Options && Options->LobbyId)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);   // Intern races the receive thread otherwise
			IdPtr = Intern(Options->LobbyId);
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, IdPtr] {
			EOS_Lobby_HardMuteMemberCallbackInfo I = {};
			I.ResultCode = EOS_EResult::EOS_Success; I.ClientData = ClientData; I.LobbyId = IdPtr; Cb(&I); });
	}

	// --- invites -----------------------------------------------------------

	void LobbyInterface::SendInvite(const EOS_Lobby_SendInviteOptions* Options, void* ClientData, EOS_Lobby_OnSendInviteCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_InvalidParameters;
		const char* IdPtr = nullptr;
		if (Options != nullptr && Options->LobbyId != nullptr && Options->TargetUserId != nullptr)
		{
			{
				std::lock_guard<std::mutex> Lock(Mutex_);   // Intern races the receive thread otherwise
				IdPtr = Intern(Options->LobbyId);
			}
			net::Endpoint To = Platform_.Peers().EndpointFor(Options->TargetUserId);
			if (To.Valid())
			{
				net::ByteWriter W;
				W.Str(Options->LobbyId);
				W.Str(LocalPid());                                   // from
				W.Str(Ids::ProductString(Options->TargetUserId));    // to
				Platform_.Net().SendTo(To, net::MessageType::LobbyInvite, W.Data().data(), W.Size());
			}
			Result = EOS_EResult::EOS_Success;
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result, IdPtr] {
			EOS_Lobby_SendInviteCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData; I.LobbyId = IdPtr; Cb(&I); });
	}

	void LobbyInterface::RejectInvite(const EOS_Lobby_RejectInviteOptions* Options, void* ClientData, EOS_Lobby_OnRejectInviteCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		EOS_EResult Result = EOS_EResult::EOS_NotFound;
		if (Options != nullptr && Options->InviteId != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (auto It = Invites_.begin(); It != Invites_.end(); ++It)
				if (It->InviteId == Options->InviteId) { Invites_.erase(It); Result = EOS_EResult::EOS_Success; break; }
		}
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, Result] {
			EOS_Lobby_RejectInviteCallbackInfo I = {};
			I.ResultCode = Result; I.ClientData = ClientData; Cb(&I); });
	}

	void LobbyInterface::QueryInvites(const EOS_Lobby_QueryInvitesOptions* Options, void* ClientData, EOS_Lobby_OnQueryInvitesCallback Cb)
	{
		EOSEMU_TRACE(Lobby);
		// Invites arrive live over the LAN; nothing to fetch. Report success so
		// the subsequent GetInviteCount enumeration runs.
		EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : nullptr;
		if (Cb) Platform_.Dispatch().Post([Cb, ClientData, User] {
			EOS_Lobby_QueryInvitesCallbackInfo I = {};
			I.ResultCode = EOS_EResult::EOS_Success; I.ClientData = ClientData; I.LocalUserId = User; Cb(&I); });
	}

	uint32_t LobbyInterface::GetInviteCount(EOS_ProductUserId)
	{
		EOSEMU_TRACE(Lobby);
		std::lock_guard<std::mutex> Lock(Mutex_);
		return static_cast<uint32_t>(Invites_.size());
	}

	EOS_EResult LobbyInterface::GetInviteIdByIndex(EOS_ProductUserId, uint32_t Index, char* OutBuffer, int32_t* InOutLen)
	{
		EOSEMU_TRACE(Lobby);
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

	EOS_EResult LobbyInterface::CopyLobbyDetailsHandleByInviteId(const char* InviteId, EOS_HLobbyDetails* Out)
	{
		EOSEMU_TRACE(Lobby);
		if (Out == nullptr || InviteId == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Inv : Invites_)
		{
			if (Inv.InviteId == InviteId)
			{
				auto It = Known_.find(Inv.LobbyId);
				if (It == Known_.end()) return EOS_EResult::EOS_NotFound;
				auto* Details = new LobbyDetailsObj();
				Details->Record = It->second;
				*Out = reinterpret_cast<EOS_HLobbyDetails>(Details);
				return EOS_EResult::EOS_Success;
			}
		}
		return EOS_EResult::EOS_NotFound;
	}

	// --- overlay-driven invite-accept / join-friend ------------------------

	std::vector<LobbyInviteView> LobbyInterface::SnapshotInvites()
	{
		std::vector<LobbyInviteView> Out;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Inv : Invites_)
		{
			// Hide invites for lobbies we already belong to -- once accepted and
			// joined, the row drops out on its own.
			if (MemberOf_.count(Inv.LobbyId)) continue;
			Out.push_back({Inv.InviteId, Inv.LobbyId, Inv.FromProduct});
		}
		return Out;
	}

	std::vector<LobbyJoinableView> LobbyInterface::SnapshotJoinable()
	{
		std::vector<LobbyJoinableView> Out;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Kv : Known_)
		{
			const LobbyRecord& Rec = Kv.second;
			if (MemberOf_.count(Rec.LobbyId) || Owned_.count(Rec.LobbyId)) continue;
			if (Rec.AvailableSlots() == 0) continue;
			Out.push_back({Rec.LobbyId, Rec.OwnerProductId});
		}
		return Out;
	}

	void LobbyInterface::AcceptInviteFromOverlay(const std::string& InviteId)
	{
		EOSEMU_TRACE(Lobby);
		std::string LobbyId, FromPid;
		const char* InvPtr = nullptr;
		const char* IdPtr = nullptr;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			bool Found = false;
			for (const auto& Inv : Invites_)
				if (Inv.InviteId == InviteId) { LobbyId = Inv.LobbyId; FromPid = Inv.FromProduct; Found = true; break; }
			if (!Found) return;
			// Keep the invite in Invites_ so the game's handler can resolve it via
			// CopyLobbyDetailsHandleByInviteId; SnapshotInvites hides it once we join.
			InvPtr = Intern(InviteId);
			IdPtr = Intern(LobbyId);
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		EOS_ProductUserId Sender = Ids::InternProduct(FromPid);
		for (const auto& E : InviteAccepted_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, InvPtr, IdPtr, Local, Sender] {
				EOS_Lobby_LobbyInviteAcceptedCallbackInfo I = {};
				I.ClientData = Cd; I.InviteId = InvPtr; I.LocalUserId = Local; I.TargetUserId = Sender; I.LobbyId = IdPtr;
				Fn(&I);
			});
		}
		EOSEMU_INFO(Lobby, "invite %s accepted via overlay", InviteId.c_str());
	}

	void LobbyInterface::RejectInviteFromOverlay(const std::string& InviteId)
	{
		EOSEMU_TRACE(Lobby);
		std::string LobbyId, FromPid;
		const char* InvPtr = nullptr;
		const char* IdPtr = nullptr;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			bool Found = false;
			for (auto It = Invites_.begin(); It != Invites_.end(); ++It)
				if (It->InviteId == InviteId) { LobbyId = It->LobbyId; FromPid = It->FromProduct; Invites_.erase(It); Found = true; break; }
			if (!Found) return;
			InvPtr = Intern(InviteId);
			IdPtr = Intern(LobbyId);
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		EOS_ProductUserId Sender = Ids::InternProduct(FromPid);
		for (const auto& E : InviteRejected_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, InvPtr, IdPtr, Local, Sender] {
				EOS_Lobby_LobbyInviteRejectedCallbackInfo I = {};
				I.ClientData = Cd; I.InviteId = InvPtr; I.LocalUserId = Local; I.TargetUserId = Sender; I.LobbyId = IdPtr;
				Fn(&I);
			});
		}
	}

	void LobbyInterface::BeginJoinFromOverlay(const std::string& LobbyId)
	{
		EOSEMU_TRACE(Lobby);
		uint64_t EventId = 0;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			auto It = Known_.find(LobbyId);
			if (It == Known_.end()) return;
			EventId = NextUiEventId();
			UiEvents_[EventId] = It->second;
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		for (const auto& E : JoinAccepted_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, Local, EventId] {
				EOS_Lobby_JoinLobbyAcceptedCallbackInfo I = {};
				I.ClientData = Cd; I.LocalUserId = Local; I.UiEventId = EventId;
				Fn(&I);
			});
		}
		EOSEMU_INFO(Lobby, "join lobby %s requested via overlay (event %llu)",
			LobbyId.c_str(), static_cast<unsigned long long>(EventId));
	}

	EOS_EResult LobbyInterface::CopyDetailsByUiEventId(uint64_t UiEventId, EOS_HLobbyDetails* Out)
	{
		EOSEMU_TRACE(Lobby);
		if (Out == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = UiEvents_.find(UiEventId);
		if (It == UiEvents_.end()) return EOS_EResult::EOS_NotFound;
		auto* Details = new LobbyDetailsObj();
		Details->Record = It->second;
		*Out = reinterpret_cast<EOS_HLobbyDetails>(Details);
		return EOS_EResult::EOS_Success;
	}

	void LobbyInterface::AcknowledgeUiEvent(uint64_t UiEventId)
	{
		EOSEMU_TRACE(Lobby);
		std::lock_guard<std::mutex> Lock(Mutex_);
		UiEvents_.erase(UiEventId);
	}

	// --- details / search --------------------------------------------------

	EOS_EResult LobbyInterface::CopyLobbyDetailsHandle(const EOS_Lobby_CopyLobbyDetailsHandleOptions* Options, EOS_HLobbyDetails* Out)
	{
		EOSEMU_TRACE(Lobby);
		if (Options == nullptr || Out == nullptr || Options->LobbyId == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Known_.find(Options->LobbyId);
		if (It == Known_.end()) return EOS_EResult::EOS_NotFound;
		auto* Details = new LobbyDetailsObj();
		Details->Record = It->second;
		Details->LocalUser = Options->LocalUserId ? Ids::ProductString(Options->LocalUserId) : std::string();
		*Out = reinterpret_cast<EOS_HLobbyDetails>(Details);
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult LobbyInterface::CreateLobbySearch(const EOS_Lobby_CreateLobbySearchOptions* Options, EOS_HLobbySearch* Out)
	{
		EOSEMU_TRACE(Lobby);
		if (Out == nullptr) return EOS_EResult::EOS_InvalidParameters;
		auto* Search = new LobbySearchObj();
		if (Options != nullptr && Options->MaxResults > 0) Search->MaxResults = Options->MaxResults;
		*Out = reinterpret_cast<EOS_HLobbySearch>(Search);
		return EOS_EResult::EOS_Success;
	}

	bool LobbyInterface::MatchesSearch(const LobbySearchObj& Search, const LobbyRecord& Rec) const
	{
		if (!Search.ByLobbyId.empty()) return Rec.LobbyId == Search.ByLobbyId;
		if (!Search.ByTargetUser.empty()) return Rec.FindMember(Search.ByTargetUser) != nullptr;

		for (const auto& P : Search.Params)
		{
			if (P.Key == EOS_LOBBY_SEARCH_BUCKET_ID)
			{
				if (Rec.BucketId != P.Value.Str) return false;
				continue;
			}
			if (P.Key == EOS_LOBBY_SEARCH_MINCURRENTMEMBERS)
			{
				if (static_cast<int64_t>(Rec.Members.size()) < P.Value.Int) return false;
				continue;
			}
			if (P.Key == EOS_LOBBY_SEARCH_MINSLOTSAVAILABLE)
			{
				if (static_cast<int64_t>(Rec.AvailableSlots()) < P.Value.Int) return false;
				continue;
			}
			// Arbitrary attribute comparison against a public lobby attribute.
			bool Matched = false;
			for (const auto& A : Rec.Attributes)
			{
				if (A.first == P.Key && A.second.Visibility == EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC
					&& CompareAttr(A.second.Value, P.Op, P.Value)) { Matched = true; break; }
			}
			if (!Matched) return false;
		}
		return true;
	}

	void LobbyInterface::CollectSearchResults(LobbySearchObj& Search)
	{
		EOSEMU_TRACE(Lobby);
		std::lock_guard<std::mutex> Lock(Mutex_);
		Search.Results.clear();
		for (const auto& Kv : Known_)
		{
			if (Search.Results.size() >= Search.MaxResults) break;
			if (MatchesSearch(Search, Kv.second))
			{
				Search.Results.push_back(Kv.second);
			}
		}
	}

	// --- RTC / connect string ----------------------------------------------

	EOS_EResult LobbyInterface::GetRTCRoomName(const EOS_Lobby_GetRTCRoomNameOptions* Options, char* OutBuffer, uint32_t* InOutLen)
	{
		EOSEMU_TRACE(Lobby);
		if (Options == nullptr || Options->LobbyId == nullptr || InOutLen == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::string Name = std::string("rtc_") + Options->LobbyId;
		const uint32_t Required = static_cast<uint32_t>(Name.size()) + 1;
		if (OutBuffer == nullptr || *InOutLen < Required) { *InOutLen = Required; return EOS_EResult::EOS_LimitExceeded; }
		std::memcpy(OutBuffer, Name.c_str(), Name.size() + 1);
		*InOutLen = Required;
		return EOS_EResult::EOS_Success;
	}

	std::vector<LobbyRTCRoomView> LobbyInterface::SnapshotRTCRooms()
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		std::vector<LobbyRTCRoomView> Out;
		for (const std::string& LobbyId : MemberOf_)   // owners are members too
		{
			auto It = Known_.find(LobbyId);
			if (It == Known_.end() || It->second.bRTCRoomEnabled == EOS_FALSE) continue;
			LobbyRTCRoomView View;
			View.LobbyId = LobbyId;
			View.RoomName = "rtc_" + LobbyId;   // must match GetRTCRoomName
			for (const LobbyMemberRec& M : It->second.Members)
			{
				View.MemberPids.push_back(M.ProductId);
			}
			Out.push_back(std::move(View));
		}
		return Out;
	}

	EOS_EResult LobbyInterface::GetConnectString(const EOS_Lobby_GetConnectStringOptions* Options, char* OutBuffer, uint32_t* InOutLen)
	{
		EOSEMU_TRACE(Lobby);
		if (Options == nullptr || Options->LobbyId == nullptr || InOutLen == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::string Connect = std::string("eosemu:lobby:") + Options->LobbyId;
		const uint32_t Required = static_cast<uint32_t>(Connect.size()) + 1;
		if (OutBuffer == nullptr || *InOutLen < Required) { *InOutLen = Required; return EOS_EResult::EOS_LimitExceeded; }
		std::memcpy(OutBuffer, Connect.c_str(), Connect.size() + 1);
		*InOutLen = Required;
		return EOS_EResult::EOS_Success;
	}

	// --- inbound datagrams -------------------------------------------------

	void LobbyInterface::OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len)
	{
		net::ByteReader R(Payload, Len);
		switch (Type)
		{
		case net::MessageType::LobbyAnnounce:
		{
			LobbyRecord Fresh;
			if (!DeserializeRecord(R, Fresh) || Fresh.LobbyId.empty()) return;
			Fresh.HostEndpoint = From; // the owner's real source endpoint
			Fresh.LastSeenTick = Platform_.TickCount();

			bool FireUpdate = false;
			std::vector<std::string> Joined, Left, Changed;
			std::string LobbyId = Fresh.LobbyId;
			std::string PromotedPid;
			bool AmMember = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				if (Owned_.count(LobbyId)) return; // we are the authority; ignore
				auto It = Known_.find(LobbyId);
				AmMember = MemberOf_.count(LobbyId) > 0;
				if (It != Known_.end() && AmMember)
				{
					const LobbyRecord& Old = It->second;
					if (Fresh.Version != Old.Version) FireUpdate = true;
					if (Old.OwnerProductId != Fresh.OwnerProductId) PromotedPid = Fresh.OwnerProductId;
					for (const auto& M : Fresh.Members)
						if (!Old.FindMember(M.ProductId)) Joined.push_back(M.ProductId);
					for (const auto& M : Old.Members)
						if (!Fresh.FindMember(M.ProductId)) Left.push_back(M.ProductId);
					// Member attribute changes, by value -- comparing counts
					// alone misses the common case of an updated value.
					for (const auto& M : Fresh.Members)
					{
						const LobbyMemberRec* OldM = Old.FindMember(M.ProductId);
						if (OldM && !MemberAttrsEqual(OldM->Attributes, M.Attributes)) Changed.push_back(M.ProductId);
					}
				}
				Known_[LobbyId] = Fresh;
				// Host migration: an announce naming us the owner hands us
				// authority. Adopt, so we answer join requests, service member
				// updates, and take over the periodic announce.
				if (AmMember && Fresh.OwnerProductId == LocalPid())
				{
					Owned_.insert(LobbyId);
				}
			}

			if (AmMember)
			{
				const char* IdPtr = nullptr;
				{ std::lock_guard<std::mutex> Lock(Mutex_); IdPtr = Intern(LobbyId); }
				if (FireUpdate)
				{
					for (const auto& E : UpdateReceived_.Snapshot())
					{
						auto Fn = E.Fn; void* Cd = E.ClientData;
						Platform_.Dispatch().Post([Fn, Cd, IdPtr] {
							EOS_Lobby_LobbyUpdateReceivedCallbackInfo I = {}; I.ClientData = Cd; I.LobbyId = IdPtr; Fn(&I); });
					}
				}
				for (const std::string& Pid : Joined)
				{
					EOS_ProductUserId Target = Ids::InternProduct(Pid);
					for (const auto& E : MemberStatusReceived_.Snapshot())
					{
						auto Fn = E.Fn; void* Cd = E.ClientData;
						Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target] {
							EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo I = {};
							I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target; I.CurrentStatus = EOS_ELobbyMemberStatus::EOS_LMS_JOINED; Fn(&I); });
					}
				}
				for (const std::string& Pid : Left)
				{
					EOS_ProductUserId Target = Ids::InternProduct(Pid);
					for (const auto& E : MemberStatusReceived_.Snapshot())
					{
						auto Fn = E.Fn; void* Cd = E.ClientData;
						Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target] {
							EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo I = {};
							I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target; I.CurrentStatus = EOS_ELobbyMemberStatus::EOS_LMS_LEFT; Fn(&I); });
					}
				}
				for (const std::string& Pid : Changed)
				{
					EOS_ProductUserId Target = Ids::InternProduct(Pid);
					for (const auto& E : MemberUpdateReceived_.Snapshot())
					{
						auto Fn = E.Fn; void* Cd = E.ClientData;
						Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target] {
							EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo I = {};
							I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target; Fn(&I); });
					}
				}
				if (!PromotedPid.empty())
				{
					EOS_ProductUserId Target = Ids::InternProduct(PromotedPid);
					for (const auto& E : MemberStatusReceived_.Snapshot())
					{
						auto Fn = E.Fn; void* Cd = E.ClientData;
						Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target] {
							EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo I = {};
							I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target;
							I.CurrentStatus = EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED; Fn(&I); });
					}
				}
			}
			break;
		}
		case net::MessageType::LobbyUpdate:
		{
			// A member forwarding its own member-attribute modification for us
			// (the owner) to merge and re-announce. Scope is self-only by
			// construction: the payload names one member and we only touch that
			// member's attribute list.
			const std::string LobbyId = R.Str();
			const std::string MemberPid = R.Str();
			AttrList Adds;
			const uint32_t AddCount = R.U32();
			for (uint32_t i = 0; i < AddCount && R.Ok(); ++i)
			{
				std::string Key; LobbyAttr A;
				ReadAttrWire(R, Key, A.Value);
				A.Visibility = static_cast<EOS_ELobbyAttributeVisibility>(R.U8());
				Adds.emplace_back(Key, A);
			}
			std::vector<std::string> Removes;
			const uint32_t RemoveCount = R.U32();
			for (uint32_t i = 0; i < RemoveCount && R.Ok(); ++i) Removes.push_back(R.Str());
			if (!R.Ok()) return;

			LobbyRecord ToBroadcast; bool DoBroadcast = false; const char* IdPtr = nullptr;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				auto It = Known_.find(LobbyId);
				if (It == Known_.end() || !Owned_.count(LobbyId)) return; // only the owner merges
				LobbyMemberRec* M = It->second.FindMember(MemberPid);
				if (M == nullptr) return;
				ApplyMemberAttrs(*M, Adds, Removes);
				It->second.Version++;
				IdPtr = Intern(LobbyId);
				ToBroadcast = It->second; DoBroadcast = true;
			}
			if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
			// The owner ignores its own announces, so surface the change here.
			{
				EOS_ProductUserId Target = Ids::InternProduct(MemberPid);
				for (const auto& E : MemberUpdateReceived_.Snapshot())
				{
					auto Fn = E.Fn; void* Cd = E.ClientData;
					Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target] {
						EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo I = {};
						I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target; Fn(&I); });
				}
			}
			break;
		}
		case net::MessageType::LobbyJoinRequest:
		{
			const std::string LobbyId = R.Str();
			const std::string JoinerPid = R.Str();
			if (!R.Ok()) return;
			LobbyRecord ToBroadcast; bool DoBroadcast = false;
			const char* IdPtr = nullptr;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				auto It = Known_.find(LobbyId);
				if (It != Known_.end() && Owned_.count(LobbyId))
				{
					if (!It->second.FindMember(JoinerPid) && It->second.AvailableSlots() > 0)
					{
						LobbyMemberRec M; M.ProductId = JoinerPid;
						It->second.Members.push_back(M);
						It->second.Version++;
						ToBroadcast = It->second; DoBroadcast = true;
						IdPtr = Intern(LobbyId);
					}
				}
			}
			if (DoBroadcast)
			{
				BroadcastAnnounce(ToBroadcast);
				// The owner ignores its own announces (it is the authority), so
				// fire its member-joined notification here or it never sees one.
				EOS_ProductUserId Target = Ids::InternProduct(JoinerPid);
				for (const auto& E : MemberStatusReceived_.Snapshot())
				{
					auto Fn = E.Fn; void* Cd = E.ClientData;
					Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target] {
						EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo I = {};
						I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target;
						I.CurrentStatus = EOS_ELobbyMemberStatus::EOS_LMS_JOINED; Fn(&I); });
				}
			}
			break;
		}
		case net::MessageType::LobbyLeave:
		{
			const std::string LobbyId = R.Str();
			const std::string SubjectPid = R.Str();
			const uint8_t ReasonByte = R.U8();
			if (!R.Ok()) return;
			const auto Status = static_cast<EOS_ELobbyMemberStatus>(ReasonByte);

			bool AmMember = false; const char* IdPtr = nullptr; bool RemovedLobby = false;
			LobbyRecord ToBroadcast; bool DoBroadcast = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				auto It = Known_.find(LobbyId);
				if (It == Known_.end()) return;
				AmMember = MemberOf_.count(LobbyId) > 0;
				IdPtr = Intern(LobbyId);

				if (Status == EOS_ELobbyMemberStatus::EOS_LMS_CLOSED)
				{
					Known_.erase(LobbyId); MemberOf_.erase(LobbyId); Owned_.erase(LobbyId);
					RemovedLobby = true;
				}
				else
				{
					for (auto Mit = It->second.Members.begin(); Mit != It->second.Members.end(); ++Mit)
						if (Mit->ProductId == SubjectPid) { It->second.Members.erase(Mit); break; }
					if (Owned_.count(LobbyId)) { It->second.Version++; ToBroadcast = It->second; DoBroadcast = true; }
					if (SubjectPid == LocalPid()) { MemberOf_.erase(LobbyId); }
				}
			}
			if (DoBroadcast) BroadcastAnnounce(ToBroadcast);
			if (AmMember)
			{
				EOS_ProductUserId Target = Ids::InternProduct(SubjectPid);
				for (const auto& E : MemberStatusReceived_.Snapshot())
				{
					auto Fn = E.Fn; void* Cd = E.ClientData;
					Platform_.Dispatch().Post([Fn, Cd, IdPtr, Target, Status] {
						EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo I = {};
						I.ClientData = Cd; I.LobbyId = IdPtr; I.TargetUserId = Target; I.CurrentStatus = Status; Fn(&I); });
				}
			}
			(void)RemovedLobby;
			break;
		}
		case net::MessageType::LobbyInvite:
		{
			const std::string LobbyId = R.Str();
			const std::string FromPid = R.Str();
			const std::string ToPid = R.Str();
			if (!R.Ok()) return;
			if (ToPid != LocalPid()) return; // not for us
			std::string InviteId;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				char Buf[64];
				std::snprintf(Buf, sizeof(Buf), "invite_%llu", static_cast<unsigned long long>(NextInvite_++));
				InviteId = Buf;
				Invites_.push_back({InviteId, LobbyId, FromPid});
			}
			EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
			EOS_ProductUserId Sender = Ids::InternProduct(FromPid);
			const char* InvPtr = nullptr;
			{ std::lock_guard<std::mutex> Lock(Mutex_); InvPtr = Intern(InviteId); }
			for (const auto& E : InviteReceived_.Snapshot())
			{
				auto Fn = E.Fn; void* Cd = E.ClientData;
				Platform_.Dispatch().Post([Fn, Cd, InvPtr, Local, Sender] {
					EOS_Lobby_LobbyInviteReceivedCallbackInfo I = {};
					I.ClientData = Cd; I.InviteId = InvPtr; I.LocalUserId = Local; I.TargetUserId = Sender; Fn(&I); });
			}
			break;
		}
		default:
			break;
		}
	}
}
