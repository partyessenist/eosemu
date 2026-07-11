//
// Sessions exported entry points.
//
// Thin translation over SessionsInterface and the handle objects
// (SessionModificationObj / SessionDetailsObj / ActiveSessionObj /
// SessionSearchObj). Copy* builders allocate API blocks freed by their matching
// *_Release; Get* borrow.
//

#include "interfaces/Sessions.h"
#include "core/Logging.h"

#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include <cstring>

using namespace EOSEmu;

namespace
{
	SessionModificationObj* AsMod(EOS_HSessionModification H) { return reinterpret_cast<SessionModificationObj*>(H); }
	SessionDetailsObj* AsDetails(EOS_HSessionDetails H) { return reinterpret_cast<SessionDetailsObj*>(H); }
	ActiveSessionObj* AsActive(EOS_HActiveSession H) { return reinterpret_cast<ActiveSessionObj*>(H); }
	SessionSearchObj* AsSearch(EOS_HSessionSearch H) { return reinterpret_cast<SessionSearchObj*>(H); }

	SessionsInterface* Iface() { Platform* P = CurrentPlatform(); return P ? &P->Sessions() : nullptr; }

	SessionAttr AttrFromData(const EOS_Sessions_AttributeData* Data, EOS_ESessionAttributeAdvertisementType Adv)
	{
		SessionAttr A;
		A.Adv = Adv;
		if (Data != nullptr)
			A.Value = ReadAttr(Data->ValueType, Data->Value.AsInt64, Data->Value.AsDouble, Data->Value.AsBool, Data->Value.AsUtf8);
		return A;
	}

	EOS_SessionDetails_Attribute* MakeAttribute(const std::string& Key, const SessionAttr& A)
	{
		EOS_SessionDetails_Attribute* Out = AllocApi<EOS_SessionDetails_Attribute>();
		Out->ApiVersion = EOS_SESSIONDETAILS_ATTRIBUTE_API_LATEST;
		Out->AdvertisementType = A.Adv;
		auto* Data = AttachArray<EOS_Sessions_AttributeData>(Out, 1);
		Data->ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
		Data->Key = AttachString(Out, Key.c_str());
		Data->ValueType = A.Value.Type;
		switch (A.Value.Type)
		{
		case EOS_EAttributeType::EOS_AT_BOOLEAN: Data->Value.AsBool = A.Value.Bool; break;
		case EOS_EAttributeType::EOS_AT_INT64: Data->Value.AsInt64 = A.Value.Int; break;
		case EOS_EAttributeType::EOS_AT_DOUBLE: Data->Value.AsDouble = A.Value.Dbl; break;
		case EOS_EAttributeType::EOS_AT_STRING: Data->Value.AsUtf8 = AttachString(Out, A.Value.Str.c_str()); break;
		default: break;
		}
		Out->Data = Data;
		return Out;
	}

	// Fills a EOS_SessionDetails_Info (+ nested Settings) as one API block.
	EOS_SessionDetails_Info* MakeInfo(const SessionRecord& Rec)
	{
		EOS_SessionDetails_Info* Info = AllocApi<EOS_SessionDetails_Info>();
		Info->ApiVersion = EOS_SESSIONDETAILS_INFO_API_LATEST;
		Info->SessionId = AttachString(Info, Rec.SessionId.c_str());
		Info->HostAddress = AttachString(Info, Rec.HostAddress.c_str());
		Info->NumOpenPublicConnections = Rec.OpenSlots();
		Info->OwnerUserId = Ids::InternProduct(Rec.OwnerProductId);
		Info->OwnerServerClientId = nullptr;

		auto* Settings = AttachArray<EOS_SessionDetails_Settings>(Info, 1);
		Settings->ApiVersion = EOS_SESSIONDETAILS_SETTINGS_API_LATEST;
		Settings->BucketId = AttachString(Info, Rec.BucketId.c_str());
		Settings->NumPublicConnections = Rec.MaxPlayers;
		Settings->bAllowJoinInProgress = Rec.bAllowJoinInProgress;
		Settings->PermissionLevel = Rec.Permission;
		Settings->bInvitesAllowed = Rec.bInvitesAllowed;
		Settings->bSanctionsEnabled = Rec.bSanctionsEnabled;
		if (!Rec.AllowedPlatformIds.empty())
		{
			uint32_t* Arr = AttachArray<uint32_t>(Info, Rec.AllowedPlatformIds.size());
			for (size_t i = 0; i < Rec.AllowedPlatformIds.size(); ++i) Arr[i] = Rec.AllowedPlatformIds[i];
			Settings->AllowedPlatformIds = Arr;
			Settings->AllowedPlatformIdsCount = static_cast<uint32_t>(Rec.AllowedPlatformIds.size());
		}
		Info->Settings = Settings;
		return Info;
	}
}

// ---------------------------------------------------------------------------
// EOS_Sessions
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CreateSessionModification(EOS_HSessions Handle, const EOS_Sessions_CreateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	return I ? I->CreateSessionModification(Options, OutSessionModificationHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_UpdateSessionModification(EOS_HSessions Handle, const EOS_Sessions_UpdateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	return I ? I->UpdateSessionModification(Options, OutSessionModificationHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_Sessions_UpdateSession(EOS_HSessions Handle, const EOS_Sessions_UpdateSessionOptions* Options, void* ClientData, const EOS_Sessions_OnUpdateSessionCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->UpdateSession(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_DestroySession(EOS_HSessions Handle, const EOS_Sessions_DestroySessionOptions* Options, void* ClientData, const EOS_Sessions_OnDestroySessionCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->DestroySession(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_JoinSession(EOS_HSessions Handle, const EOS_Sessions_JoinSessionOptions* Options, void* ClientData, const EOS_Sessions_OnJoinSessionCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->JoinSession(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_StartSession(EOS_HSessions Handle, const EOS_Sessions_StartSessionOptions* Options, void* ClientData, const EOS_Sessions_OnStartSessionCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->StartSession(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_EndSession(EOS_HSessions Handle, const EOS_Sessions_EndSessionOptions* Options, void* ClientData, const EOS_Sessions_OnEndSessionCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->EndSession(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RegisterPlayers(EOS_HSessions Handle, const EOS_Sessions_RegisterPlayersOptions* Options, void* ClientData, const EOS_Sessions_OnRegisterPlayersCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->RegisterPlayers(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_UnregisterPlayers(EOS_HSessions Handle, const EOS_Sessions_UnregisterPlayersOptions* Options, void* ClientData, const EOS_Sessions_OnUnregisterPlayersCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->UnregisterPlayers(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_SendInvite(EOS_HSessions Handle, const EOS_Sessions_SendInviteOptions* Options, void* ClientData, const EOS_Sessions_OnSendInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->SendInvite(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_RejectInvite(EOS_HSessions Handle, const EOS_Sessions_RejectInviteOptions* Options, void* ClientData, const EOS_Sessions_OnRejectInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->RejectInvite(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Sessions_QueryInvites(EOS_HSessions Handle, const EOS_Sessions_QueryInvitesOptions* Options, void* ClientData, const EOS_Sessions_OnQueryInvitesCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<SessionsInterface>(Handle)) I->QueryInvites(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Sessions_GetInviteCount(EOS_HSessions Handle, const EOS_Sessions_GetInviteCountOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	return I ? I->GetInviteCount(Options ? Options->LocalUserId : nullptr) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_GetInviteIdByIndex(EOS_HSessions Handle, const EOS_Sessions_GetInviteIdByIndexOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->GetInviteIdByIndex(Options->LocalUserId, Options->Index, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CreateSessionSearch(EOS_HSessions Handle, const EOS_Sessions_CreateSessionSearchOptions* Options, EOS_HSessionSearch* OutSessionSearchHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	return I ? I->CreateSessionSearch(Options, OutSessionSearchHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopyActiveSessionHandle(EOS_HSessions Handle, const EOS_Sessions_CopyActiveSessionHandleOptions* Options, EOS_HActiveSession* OutSessionHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopyActiveSessionHandle(Options->SessionName, OutSessionHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleByInviteId(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleByInviteIdOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopySessionHandleByInviteId(Options->InviteId, OutSessionHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleByUiEventId(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleByUiEventIdOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	// Resolves a UiEventId minted when the user chose "join" on a discovered
	// session in the overlay (BeginJoinFromOverlay / JoinSessionAccepted).
	return I->CopyDetailsByUiEventId(Options->UiEventId, OutSessionHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_CopySessionHandleForPresence(EOS_HSessions Handle, const EOS_Sessions_CopySessionHandleForPresenceOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || OutSessionHandle == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutSessionHandle = nullptr;
	// Return the first presence-enabled session we participate in, if any.
	// Simpler: no presence session tracked -> NotFound.
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_IsUserInSession(EOS_HSessions Handle, const EOS_Sessions_IsUserInSessionOptions* Options, EOS_ProductUserId TargetUserId)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->IsUserInSession(Options->SessionName, TargetUserId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Sessions_DumpSessionState(EOS_HSessions Handle, const EOS_Sessions_DumpSessionStateOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<SessionsInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->DumpSessionState(Options->SessionName);
}

// Notifications
EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteReceived(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteReceivedOptions* Options, void* ClientData, const EOS_Sessions_OnSessionInviteReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<SessionsInterface>(Handle); return I ? I->InviteReceived_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteReceived(EOS_HSessions Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<SessionsInterface>(Handle)) I->InviteReceived_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteAccepted(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteAcceptedOptions* Options, void* ClientData, const EOS_Sessions_OnSessionInviteAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<SessionsInterface>(Handle); return I ? I->InviteAccepted_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteAccepted(EOS_HSessions Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<SessionsInterface>(Handle)) I->InviteAccepted_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySessionInviteRejected(EOS_HSessions Handle, const EOS_Sessions_AddNotifySessionInviteRejectedOptions* Options, void* ClientData, const EOS_Sessions_OnSessionInviteRejectedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<SessionsInterface>(Handle); return I ? I->InviteRejected_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySessionInviteRejected(EOS_HSessions Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<SessionsInterface>(Handle)) I->InviteRejected_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifyJoinSessionAccepted(EOS_HSessions Handle, const EOS_Sessions_AddNotifyJoinSessionAcceptedOptions* Options, void* ClientData, const EOS_Sessions_OnJoinSessionAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<SessionsInterface>(Handle); return I ? I->JoinAccepted_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifyJoinSessionAccepted(EOS_HSessions Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<SessionsInterface>(Handle)) I->JoinAccepted_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifyLeaveSessionRequested(EOS_HSessions Handle, const EOS_Sessions_AddNotifyLeaveSessionRequestedOptions* Options, void* ClientData, const EOS_Sessions_OnLeaveSessionRequestedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<SessionsInterface>(Handle); return I ? I->LeaveRequested_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifyLeaveSessionRequested(EOS_HSessions Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<SessionsInterface>(Handle)) I->LeaveRequested_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Sessions_AddNotifySendSessionNativeInviteRequested(EOS_HSessions Handle, const EOS_Sessions_AddNotifySendSessionNativeInviteRequestedOptions* Options, void* ClientData, const EOS_Sessions_OnSendSessionNativeInviteRequestedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Handle; (void)Options; (void)ClientData; (void)NotificationFn; return EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Sessions_RemoveNotifySendSessionNativeInviteRequested(EOS_HSessions Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); (void)Handle; (void)InId; }

// ---------------------------------------------------------------------------
// EOS_SessionModification mutators
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_SessionModification_Release(EOS_HSessionModification SessionModificationHandle) {
	EOSEMU_API_TRACE(); delete AsMod(SessionModificationHandle); }

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetBucketId(EOS_HSessionModification Handle, const EOS_SessionModification_SetBucketIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->BucketId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetBucket = true; M->Bucket = Options->BucketId; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetHostAddress(EOS_HSessionModification Handle, const EOS_SessionModification_SetHostAddressOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->HostAddress == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetHost = true; M->HostAddress = Options->HostAddress; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetPermissionLevel(EOS_HSessionModification Handle, const EOS_SessionModification_SetPermissionLevelOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetPerm = true; M->Perm = Options->PermissionLevel; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetJoinInProgressAllowed(EOS_HSessionModification Handle, const EOS_SessionModification_SetJoinInProgressAllowedOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetJIP = true; M->JIP = Options->bAllowJoinInProgress; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetMaxPlayers(EOS_HSessionModification Handle, const EOS_SessionModification_SetMaxPlayersOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetMax = true; M->Max = Options->MaxPlayers; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetInvitesAllowed(EOS_HSessionModification Handle, const EOS_SessionModification_SetInvitesAllowedOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetInvites = true; M->Invites = Options->bInvitesAllowed; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_SetAllowedPlatformIds(EOS_HSessionModification Handle, const EOS_SessionModification_SetAllowedPlatformIdsOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetPlatforms = true; M->Platforms.clear();
	for (uint32_t i = 0; Options->AllowedPlatformIds != nullptr && i < Options->AllowedPlatformIdsCount; ++i)
		M->Platforms.push_back(Options->AllowedPlatformIds[i]);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_AddAttribute(EOS_HSessionModification Handle, const EOS_SessionModification_AddAttributeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->SessionAttribute == nullptr || Options->SessionAttribute->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->AddAttrs.emplace_back(Options->SessionAttribute->Key, AttrFromData(Options->SessionAttribute, Options->AdvertisementType));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionModification_RemoveAttribute(EOS_HSessionModification Handle, const EOS_SessionModification_RemoveAttributeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->RemoveAttrs.push_back(Options->Key);
	return EOS_EResult::EOS_Success;
}

// ---------------------------------------------------------------------------
// EOS_ActiveSession
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_ActiveSession_Release(EOS_HActiveSession ActiveSessionHandle) {
	EOSEMU_API_TRACE(); delete AsActive(ActiveSessionHandle); }

EOS_DECLARE_FUNC(EOS_EResult) EOS_ActiveSession_CopyInfo(EOS_HActiveSession Handle, const EOS_ActiveSession_CopyInfoOptions* Options, EOS_ActiveSession_Info** OutActiveSessionInfo)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* A = AsActive(Handle);
	SessionsInterface* I = Iface();
	if (A == nullptr || I == nullptr || OutActiveSessionInfo == nullptr) return EOS_EResult::EOS_InvalidParameters;
	SessionRecord Rec;
	if (!I->CopyRecord(A->SessionName, Rec)) return EOS_EResult::EOS_NotFound;

	EOS_ActiveSession_Info* Info = AllocApi<EOS_ActiveSession_Info>();
	Info->ApiVersion = EOS_ACTIVESESSION_INFO_API_LATEST;
	Info->SessionName = AttachString(Info, Rec.SessionName.c_str());
	Info->LocalUserId = Ids::InternProduct(A->LocalUser);
	Info->State = Rec.State;
	Info->SessionDetails = MakeInfo(Rec); // separate API block; released with the parent below
	*OutActiveSessionInfo = Info;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(uint32_t) EOS_ActiveSession_GetRegisteredPlayerCount(EOS_HActiveSession Handle, const EOS_ActiveSession_GetRegisteredPlayerCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* A = AsActive(Handle);
	SessionsInterface* I = Iface();
	if (A == nullptr || I == nullptr) return 0;
	SessionRecord Rec;
	return I->CopyRecord(A->SessionName, Rec) ? static_cast<uint32_t>(Rec.RegisteredPlayers.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_ActiveSession_GetRegisteredPlayerByIndex(EOS_HActiveSession Handle, const EOS_ActiveSession_GetRegisteredPlayerByIndexOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* A = AsActive(Handle);
	SessionsInterface* I = Iface();
	if (A == nullptr || I == nullptr || Options == nullptr) return nullptr;
	SessionRecord Rec;
	if (!I->CopyRecord(A->SessionName, Rec) || Options->PlayerIndex >= Rec.RegisteredPlayers.size()) return nullptr;
	return Ids::InternProduct(Rec.RegisteredPlayers[Options->PlayerIndex]);
}

EOS_DECLARE_FUNC(void) EOS_ActiveSession_Info_Release(EOS_ActiveSession_Info* ActiveSessionInfo)
{
	EOSEMU_API_TRACE();
	if (ActiveSessionInfo != nullptr)
	{
		// The nested SessionDetails info is a separate API block.
		FreeApiBlock(const_cast<EOS_SessionDetails_Info*>(ActiveSessionInfo->SessionDetails));
	}
	FreeApiBlock(ActiveSessionInfo);
}

// ---------------------------------------------------------------------------
// EOS_SessionDetails
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Release(EOS_HSessionDetails SessionHandle) {
	EOSEMU_API_TRACE(); delete AsDetails(SessionHandle); }

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopyInfo(EOS_HSessionDetails Handle, const EOS_SessionDetails_CopyInfoOptions* Options, EOS_SessionDetails_Info** OutSessionInfo)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* D = AsDetails(Handle);
	if (D == nullptr || OutSessionInfo == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutSessionInfo = MakeInfo(D->Record);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Info_Release(EOS_SessionDetails_Info* SessionInfo) {
	EOSEMU_API_TRACE(); FreeApiBlock(SessionInfo); }

EOS_DECLARE_FUNC(uint32_t) EOS_SessionDetails_GetSessionAttributeCount(EOS_HSessionDetails Handle, const EOS_SessionDetails_GetSessionAttributeCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* D = AsDetails(Handle);
	return D ? static_cast<uint32_t>(D->Record.Attributes.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopySessionAttributeByIndex(EOS_HSessionDetails Handle, const EOS_SessionDetails_CopySessionAttributeByIndexOptions* Options, EOS_SessionDetails_Attribute** OutSessionAttribute)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || OutSessionAttribute == nullptr) return EOS_EResult::EOS_InvalidParameters;
	if (Options->AttrIndex >= D->Record.Attributes.size()) return EOS_EResult::EOS_NotFound;
	const auto& A = D->Record.Attributes[Options->AttrIndex];
	*OutSessionAttribute = MakeAttribute(A.first, A.second);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionDetails_CopySessionAttributeByKey(EOS_HSessionDetails Handle, const EOS_SessionDetails_CopySessionAttributeByKeyOptions* Options, EOS_SessionDetails_Attribute** OutSessionAttribute)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->AttrKey == nullptr || OutSessionAttribute == nullptr) return EOS_EResult::EOS_InvalidParameters;
	for (const auto& A : D->Record.Attributes)
		if (A.first == Options->AttrKey) { *OutSessionAttribute = MakeAttribute(A.first, A.second); return EOS_EResult::EOS_Success; }
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_SessionDetails_Attribute_Release(EOS_SessionDetails_Attribute* SessionAttribute) {
	EOSEMU_API_TRACE(); FreeApiBlock(SessionAttribute); }

// ---------------------------------------------------------------------------
// EOS_SessionSearch
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_SessionSearch_Release(EOS_HSessionSearch SessionSearchHandle) {
	EOSEMU_API_TRACE(); delete AsSearch(SessionSearchHandle); }

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetSessionId(EOS_HSessionSearch Handle, const EOS_SessionSearch_SetSessionIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->SessionId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	S->BySessionId = Options->SessionId; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetTargetUserId(EOS_HSessionSearch Handle, const EOS_SessionSearch_SetTargetUserIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->TargetUserId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	S->ByTargetUser = Ids::ProductString(Options->TargetUserId); return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetParameter(EOS_HSessionSearch Handle, const EOS_SessionSearch_SetParameterOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->Parameter == nullptr || Options->Parameter->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	SessionSearchObj::Param P;
	P.Key = Options->Parameter->Key;
	P.Value = ReadAttr(Options->Parameter->ValueType, Options->Parameter->Value.AsInt64, Options->Parameter->Value.AsDouble, Options->Parameter->Value.AsBool, Options->Parameter->Value.AsUtf8);
	P.Op = Options->ComparisonOp;
	for (auto& E : S->Params) if (E.Key == P.Key) { E = P; return EOS_EResult::EOS_Success; }
	S->Params.push_back(P);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_RemoveParameter(EOS_HSessionSearch Handle, const EOS_SessionSearch_RemoveParameterOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	for (auto It = S->Params.begin(); It != S->Params.end(); ++It)
		if (It->Key == Options->Key) { S->Params.erase(It); break; }
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_SetMaxResults(EOS_HSessionSearch Handle, const EOS_SessionSearch_SetMaxResultsOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->MaxSearchResults == 0) return EOS_EResult::EOS_InvalidParameters;
	S->MaxResults = Options->MaxSearchResults; return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_SessionSearch_Find(EOS_HSessionSearch Handle, const EOS_SessionSearch_FindOptions* Options, void* ClientData, const EOS_SessionSearch_OnFindCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* S = AsSearch(Handle);
	SessionsInterface* I = Iface();
	Platform* P = CurrentPlatform();
	const bool Ok = (S != nullptr && I != nullptr);
	if (Ok) I->CollectSearchResults(*S);
	if (CompletionDelegate == nullptr) return;
	const EOS_EResult Result = Ok ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters;
	auto Fire = [CompletionDelegate, ClientData, Result]
	{
		EOS_SessionSearch_FindCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	};
	// Never drop the completion; with no platform to defer through, inline is
	// the least-bad option.
	if (P) P->Dispatch().Post(Fire); else Fire();
}

EOS_DECLARE_FUNC(uint32_t) EOS_SessionSearch_GetSearchResultCount(EOS_HSessionSearch Handle, const EOS_SessionSearch_GetSearchResultCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* S = AsSearch(Handle);
	return S ? static_cast<uint32_t>(S->Results.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_SessionSearch_CopySearchResultByIndex(EOS_HSessionSearch Handle, const EOS_SessionSearch_CopySearchResultByIndexOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || OutSessionHandle == nullptr) return EOS_EResult::EOS_InvalidParameters;
	if (Options->SessionIndex >= S->Results.size()) return EOS_EResult::EOS_NotFound;
	auto* Details = new SessionDetailsObj();
	Details->Record = S->Results[Options->SessionIndex];
	*OutSessionHandle = reinterpret_cast<EOS_HSessionDetails>(Details);
	return EOS_EResult::EOS_Success;
}
