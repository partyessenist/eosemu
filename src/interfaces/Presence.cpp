//
// Presence interface.
//
// Local presence is authoritative and replicated in the Hello presence
// extension (status, rich text, join info, data records, product identity), so
// peers observe it via the peer directory. Remote CopyPresence/GetJoinInfo
// serve the peer's announced presence back verbatim -- whatever field a game
// keys "friend is in-game" on (ProductId match, an activity data record, join
// info) round-trips without interpretation. The builder
// (CreatePresenceModification -> setters -> SetPresence) matches the header.
//

#include "interfaces/Presence.h"
#include "core/Logging.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Peers.h"
#include "core/Platform.h"

#include <cstring>

namespace EOSEmu
{
	bool PresenceInterface::HasPresence(EOS_EpicAccountId Target)
	{
		if (Target == nullptr) return false;
		if (Target == Platform_.LocalIdentity().EpicAccountId()) return true;
		PeerInfo Info;
		return Platform_.Peers().FindByEpic(Target, Info);
	}

	EOS_EResult PresenceInterface::CopyPresence(EOS_EpicAccountId Target, EOS_Presence_Info** Out)
	{
		if (Out == nullptr || Target == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;

		std::string RichText;
		std::string ProductId, ProductVersion, ProductName;
		std::vector<std::pair<std::string, std::string>> Records;
		EOS_Presence_EStatus Status = EOS_Presence_EStatus::EOS_PS_Online;
		const bool IsLocal = (Target == Platform_.LocalIdentity().EpicAccountId());
		if (IsLocal)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			RichText = LocalRichText_;
			Status = LocalStatus_;
			Records.assign(LocalData_.begin(), LocalData_.end());
		}
		else
		{
			PeerInfo Info;
			if (!Platform_.Peers().FindByEpic(Target, Info)) return EOS_EResult::EOS_NotFound;
			// A peer that stopped announcing (timeout or Goodbye) stays a friend
			// but reads as Offline, overriding whatever status it last announced.
			Status = Info.Online ? static_cast<EOS_Presence_EStatus>(Info.Presence.Status)
			                     : EOS_Presence_EStatus::EOS_PS_Offline;
			RichText = Info.Presence.RichText;
			Records = Info.Presence.Data;
			ProductId = Info.Presence.ProductId;
			ProductVersion = Info.Presence.ProductVersion;
			ProductName = Info.Presence.ProductName;
		}

		// Product identity: what the peer announced (its own EOS_Platform_Options
		// / EOS_Initialize values), falling back to ours -- a peer on the same
		// LAN runs the same game, so a game comparing the friend's ProductId to
		// its own sees a match and flags the friend in-game.
		if (ProductId.empty()) ProductId = Platform_.ProductId();
		if (ProductId.empty()) ProductId = "eosemu";
		if (ProductVersion.empty()) ProductVersion = InitializedProductVersion();
		if (ProductVersion.empty()) ProductVersion = "1.0";
		if (ProductName.empty()) ProductName = InitializedProductName();
		if (ProductName.empty()) ProductName = "EOSEmu";

		EOS_Presence_Info* Presence = AllocApi<EOS_Presence_Info>();
		Presence->ApiVersion = EOS_PRESENCE_INFO_API_LATEST;
		Presence->Status = Status;
		Presence->UserId = Target;
		Presence->ProductId = AttachString(Presence, ProductId.c_str());
		Presence->ProductVersion = AttachString(Presence, ProductVersion.c_str());
		// Epic's presence service reports short platform codes; "WIN" is the
		// plausible Windows value (verify against an EOSTracer capture if a game
		// turns out to key on it).
		Presence->Platform = AttachString(Presence, "WIN");
		Presence->RichText = AttachString(Presence, RichText.c_str());
		Presence->RecordsCount = static_cast<int32_t>(Records.size());
		if (!Records.empty())
		{
			EOS_Presence_DataRecord* Arr = AttachArray<EOS_Presence_DataRecord>(Presence, Records.size());
			for (size_t i = 0; i < Records.size(); ++i)
			{
				Arr[i].ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
				Arr[i].Key = AttachString(Presence, Records[i].first.c_str());
				Arr[i].Value = AttachString(Presence, Records[i].second.c_str());
			}
			Presence->Records = Arr;
		}
		else
		{
			Presence->Records = nullptr;
		}
		Presence->ProductName = AttachString(Presence, ProductName.c_str());
		Presence->IntegratedPlatform = AttachString(Presence, "");
		*Out = Presence;
		return EOS_EResult::EOS_Success;
	}

	void PresenceInterface::SetPresence(EOS_EpicAccountId Local, PresenceModificationObj* Mod, void* ClientData, EOS_Presence_SetPresenceCompleteCallback Cb)
	{
		if (Mod != nullptr)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			if (Mod->bSetStatus) LocalStatus_ = Mod->Status;
			if (Mod->bSetRichText) { LocalRichText_ = Mod->RichText; EOSEMU_LOG(Presence, EOS_ELogLevel::EOS_LOG_VeryVerbose, "SetPresence RichText=\"%s\"", Mod->RichText.c_str()); }
			if (Mod->bSetJoinInfo) { LocalJoinInfo_ = Mod->JoinInfo; EOSEMU_LOG(Presence, EOS_ELogLevel::EOS_LOG_VeryVerbose, "SetPresence JoinInfo=\"%s\"", Mod->JoinInfo.c_str()); }
			for (const auto& Kv : Mod->SetData) { LocalData_[Kv.first] = Kv.second; EOSEMU_LOG(Presence, EOS_ELogLevel::EOS_LOG_VeryVerbose, "SetPresence Data['%s']=\"%s\"", Kv.first.c_str(), Kv.second.c_str()); }
			for (const auto& Key : Mod->DeleteData) LocalData_.erase(Key);
			// Push the whole blob into the Hello announce so peers replicate it;
			// the next Tick re-broadcasts immediately (HelloDirty).
			Platform_.SetLocalPresence(static_cast<uint8_t>(LocalStatus_), LocalRichText_,
				LocalJoinInfo_, {LocalData_.begin(), LocalData_.end()});
		}

		EOS_EpicAccountId LocalUser = (Local != nullptr) ? Local : Platform_.LocalIdentity().EpicAccountId();
		// Notify local observers that our presence changed.
		for (const auto& E : PresenceChanged_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, LocalUser]
			{
				EOS_Presence_PresenceChangedCallbackInfo Info = {};
				Info.ClientData = Cd; Info.LocalUserId = LocalUser; Info.PresenceUserId = LocalUser; Fn(&Info);
			});
		}

		if (Cb != nullptr)
		{
			Platform_.Dispatch().Post([Cb, ClientData, LocalUser]
			{
				EOS_Presence_SetPresenceCallbackInfo Info = {};
				Info.ResultCode = EOS_EResult::EOS_Success;
				Info.ClientData = ClientData;
				Info.LocalUserId = LocalUser;
				Cb(&Info);
			});
		}
	}

	EOS_EResult PresenceInterface::GetJoinInfo(EOS_EpicAccountId Target, char* OutBuffer, int32_t* InOutLen)
	{
		if (InOutLen == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::string Join;
		if (Target == Platform_.LocalIdentity().EpicAccountId())
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Join = LocalJoinInfo_;
		}
		else
		{
			PeerInfo Info;
			if (Platform_.Peers().FindByEpic(Target, Info)) Join = Info.Presence.JoinInfo;
		}
		if (Join.empty()) return EOS_EResult::EOS_NotFound;
		const int32_t Required = static_cast<int32_t>(Join.size()) + 1;
		if (OutBuffer == nullptr || *InOutLen < Required) { *InOutLen = Required; return EOS_EResult::EOS_LimitExceeded; }
		std::memcpy(OutBuffer, Join.c_str(), Join.size() + 1);
		*InOutLen = Required;
		return EOS_EResult::EOS_Success;
	}

	void PresenceInterface::OnRemotePresenceChanged(const std::string& EpicId)
	{
		NotifyPresenceChanged(Ids::InternEpic(EpicId));
	}

	void PresenceInterface::NotifyPresenceChanged(EOS_EpicAccountId Peer)
	{
		if (Peer == nullptr) return;
		EOS_EpicAccountId Local = Platform_.LocalIdentity().EpicAccountId();
		for (const auto& E : PresenceChanged_.Snapshot())
		{
			auto Fn = E.Fn; void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, Local, Peer]
			{
				EOS_Presence_PresenceChangedCallbackInfo Info = {};
				Info.ClientData = Cd; Info.LocalUserId = Local; Info.PresenceUserId = Peer; Fn(&Info);
			});
		}
	}
}

using namespace EOSEmu;

namespace
{
	PresenceModificationObj* AsMod(EOS_HPresenceModification H) { return reinterpret_cast<PresenceModificationObj*>(H); }
}

EOS_DECLARE_FUNC(void) EOS_Presence_QueryPresence(EOS_HPresence Handle, const EOS_Presence_QueryPresenceOptions* Options, void* ClientData, const EOS_Presence_OnQueryPresenceCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<PresenceInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	EOS_EpicAccountId Target = Options ? Options->TargetUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_Presence_QueryPresenceCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_Presence_HasPresence(EOS_HPresence Handle, const EOS_Presence_HasPresenceOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<PresenceInterface>(Handle);
	return (I && Options && I->HasPresence(Options->TargetUserId)) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_CopyPresence(EOS_HPresence Handle, const EOS_Presence_CopyPresenceOptions* Options, EOS_Presence_Info** OutPresence)
{
	EOSEMU_API_TRACE();
	auto* I = As<PresenceInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopyPresence(Options->TargetUserId, OutPresence);
}

EOS_DECLARE_FUNC(void) EOS_Presence_Info_Release(EOS_Presence_Info* PresenceInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(PresenceInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_CreatePresenceModification(EOS_HPresence Handle, const EOS_Presence_CreatePresenceModificationOptions* Options, EOS_HPresenceModification* OutPresenceModificationHandle)
{
	EOSEMU_API_TRACE();
	(void)Options;
	if (Handle == nullptr || OutPresenceModificationHandle == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutPresenceModificationHandle = reinterpret_cast<EOS_HPresenceModification>(new PresenceModificationObj());
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Presence_SetPresence(EOS_HPresence Handle, const EOS_Presence_SetPresenceOptions* Options, void* ClientData, const EOS_Presence_SetPresenceCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<PresenceInterface>(Handle);
	if (I == nullptr) return;
	PresenceModificationObj* Mod = Options ? AsMod(Options->PresenceModificationHandle) : nullptr;
	I->SetPresence(Options ? Options->LocalUserId : nullptr, Mod, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Presence_AddNotifyOnPresenceChanged(EOS_HPresence Handle, const EOS_Presence_AddNotifyOnPresenceChangedOptions* Options, void* ClientData, const EOS_Presence_OnPresenceChangedCallback NotificationHandler)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<PresenceInterface>(Handle);
	return I ? I->AddNotifyOnPresenceChanged(NotificationHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Presence_RemoveNotifyOnPresenceChanged(EOS_HPresence Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<PresenceInterface>(Handle)) I->RemoveNotifyOnPresenceChanged(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Presence_AddNotifyJoinGameAccepted(EOS_HPresence Handle, const EOS_Presence_AddNotifyJoinGameAcceptedOptions* Options, void* ClientData, const EOS_Presence_OnJoinGameAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<PresenceInterface>(Handle);
	return I ? I->AddNotifyJoinGameAccepted(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Presence_RemoveNotifyJoinGameAccepted(EOS_HPresence Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<PresenceInterface>(Handle)) I->RemoveNotifyJoinGameAccepted(InId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Presence_GetJoinInfo(EOS_HPresence Handle, const EOS_Presence_GetJoinInfoOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* I = As<PresenceInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->GetJoinInfo(Options->TargetUserId, OutBuffer, InOutBufferLength);
}

// --- PresenceModification mutators -----------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetStatus(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetStatusOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetStatus = true; M->Status = Options->Status;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetRawRichText(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetRawRichTextOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->RichText == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetRichText = true; M->RichText = Options->RichText;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetData(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetDataOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	for (int32_t i = 0; Options->Records != nullptr && i < Options->RecordsCount; ++i)
	{
		const auto& Rec = Options->Records[i];
		if (Rec.Key != nullptr) M->SetData.emplace_back(Rec.Key, Rec.Value ? Rec.Value : "");
	}
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_DeleteData(EOS_HPresenceModification Handle, const EOS_PresenceModification_DeleteDataOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	for (int32_t i = 0; Options->Records != nullptr && i < Options->RecordsCount; ++i)
		if (Options->Records[i].Key != nullptr) M->DeleteData.emplace_back(Options->Records[i].Key);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PresenceModification_SetJoinInfo(EOS_HPresenceModification Handle, const EOS_PresenceModification_SetJoinInfoOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetJoinInfo = true; M->JoinInfo = Options->JoinInfo ? Options->JoinInfo : "";
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_PresenceModification_Release(EOS_HPresenceModification PresenceModificationHandle)
{
	EOSEMU_API_TRACE();
	delete AsMod(PresenceModificationHandle);
}
