//
// Lobby exported entry points.
//
// Thin translation layer over LobbyInterface and the handle objects
// (LobbyModificationObj / LobbyDetailsObj / LobbySearchObj). Mutators operate on
// the handle's own state; anything touching shared lobby state routes through
// the interface. See interfaces/Lobby.cpp for the model.
//

#include "interfaces/Lobby.h"
#include "core/Logging.h"
#include "interfaces/RTC.h"

#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include <cstring>
#include <deque>
#include <mutex>

using namespace EOSEmu;

namespace
{
	LobbyModificationObj* AsMod(EOS_HLobbyModification H) { return reinterpret_cast<LobbyModificationObj*>(H); }
	LobbyDetailsObj* AsDetails(EOS_HLobbyDetails H) { return reinterpret_cast<LobbyDetailsObj*>(H); }
	LobbySearchObj* AsSearch(EOS_HLobbySearch H) { return reinterpret_cast<LobbySearchObj*>(H); }

	// Simulated backend latency for LobbySearch_Find completion. The real SDK
	// takes ~200ms; shipped consumers rely on the completion not firing on the
	// same frame as the call. See EOS_LobbySearch_Find.
	constexpr unsigned kSearchLatencyMs = 120;

	// EOS_LobbySearch_Release should free the search, but some consumers copy a
	// result *after* releasing it: one shipped game's host flow does
	// Find -> Release -> CopySearchResultByIndex, and the real SDK tolerates
	// that brief use. Freeing immediately makes the copy read freed memory and
	// return null details, which the game reports as "details fetch failed".
	// So retire released searches into a small ring and free the oldest only
	// once the ring is full -- bounded (not a leak), but a stray copy right
	// after release still finds valid results.
	std::mutex g_RetiredMutex;
	std::deque<LobbySearchObj*> g_RetiredSearches;
	void RetireSearch(LobbySearchObj* S)
	{
		if (S == nullptr) return;
		std::lock_guard<std::mutex> Lock(g_RetiredMutex);
		g_RetiredSearches.push_back(S);
		while (g_RetiredSearches.size() > 16)
		{
			delete g_RetiredSearches.front();
			g_RetiredSearches.pop_front();
		}
	}

	LobbyInterface* Iface()
	{
		Platform* P = CurrentPlatform();
		return P ? &P->Lobby() : nullptr;
	}

	// Human-readable "type:value" for an attribute, for diagnostic logging of the
	// join/connect-string flow.
	std::string AttrDesc(const AttrValue& V)
	{
		char Buf[256];
		switch (V.Type)
		{
		case EOS_EAttributeType::EOS_AT_BOOLEAN:
			std::snprintf(Buf, sizeof(Buf), "bool:%s", V.Bool ? "true" : "false"); break;
		case EOS_EAttributeType::EOS_AT_INT64:
			std::snprintf(Buf, sizeof(Buf), "int64:%lld", static_cast<long long>(V.Int)); break;
		case EOS_EAttributeType::EOS_AT_DOUBLE:
			std::snprintf(Buf, sizeof(Buf), "double:%g", V.Dbl); break;
		case EOS_EAttributeType::EOS_AT_STRING:
			std::snprintf(Buf, sizeof(Buf), "string:\"%s\"", V.Str.c_str()); break;
		default:
			std::snprintf(Buf, sizeof(Buf), "?type%d", static_cast<int>(V.Type)); break;
		}
		return Buf;
	}

	LobbyAttr AttrFromData(const EOS_Lobby_AttributeData* Data, EOS_ELobbyAttributeVisibility Vis)
	{
		LobbyAttr A;
		A.Visibility = Vis;
		if (Data != nullptr)
		{
			A.Value = ReadAttr(Data->ValueType, Data->Value.AsInt64, Data->Value.AsDouble, Data->Value.AsBool, Data->Value.AsUtf8);
		}
		return A;
	}

	// Builds a heap EOS_Lobby_Attribute (+ nested AttributeData) as one API block.
	EOS_Lobby_Attribute* MakeAttribute(const std::string& Key, const LobbyAttr& A)
	{
		EOS_Lobby_Attribute* Out = AllocApi<EOS_Lobby_Attribute>();
		Out->ApiVersion = EOS_LOBBY_ATTRIBUTE_API_LATEST;
		Out->Visibility = A.Visibility;
		auto* Data = AttachArray<EOS_Lobby_AttributeData>(Out, 1);
		Data->ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
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
}

// ---------------------------------------------------------------------------
// EOS_Lobby async + sync surface
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_Lobby_CreateLobby(EOS_HLobby Handle, const EOS_Lobby_CreateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnCreateLobbyCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->CreateLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_DestroyLobby(EOS_HLobby Handle, const EOS_Lobby_DestroyLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnDestroyLobbyCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->DestroyLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinLobby(EOS_HLobby Handle, const EOS_Lobby_JoinLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->JoinLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinLobbyById(EOS_HLobby Handle, const EOS_Lobby_JoinLobbyByIdOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyByIdCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->JoinLobbyById(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_LeaveLobby(EOS_HLobby Handle, const EOS_Lobby_LeaveLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveLobbyCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->LeaveLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_UpdateLobbyModification(EOS_HLobby Handle, const EOS_Lobby_UpdateLobbyModificationOptions* Options, EOS_HLobbyModification* OutLobbyModificationHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	return I ? I->UpdateLobbyModification(Options, OutLobbyModificationHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_UpdateLobby(EOS_HLobby Handle, const EOS_Lobby_UpdateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnUpdateLobbyCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->UpdateLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_PromoteMember(EOS_HLobby Handle, const EOS_Lobby_PromoteMemberOptions* Options, void* ClientData, const EOS_Lobby_OnPromoteMemberCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->PromoteMember(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_KickMember(EOS_HLobby Handle, const EOS_Lobby_KickMemberOptions* Options, void* ClientData, const EOS_Lobby_OnKickMemberCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->KickMember(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_HardMuteMember(EOS_HLobby Handle, const EOS_Lobby_HardMuteMemberOptions* Options, void* ClientData, const EOS_Lobby_OnHardMuteMemberCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->HardMuteMember(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_SendInvite(EOS_HLobby Handle, const EOS_Lobby_SendInviteOptions* Options, void* ClientData, const EOS_Lobby_OnSendInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->SendInvite(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RejectInvite(EOS_HLobby Handle, const EOS_Lobby_RejectInviteOptions* Options, void* ClientData, const EOS_Lobby_OnRejectInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->RejectInvite(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_QueryInvites(EOS_HLobby Handle, const EOS_Lobby_QueryInvitesOptions* Options, void* ClientData, const EOS_Lobby_OnQueryInvitesCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<LobbyInterface>(Handle)) I->QueryInvites(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Lobby_GetInviteCount(EOS_HLobby Handle, const EOS_Lobby_GetInviteCountOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	return I ? I->GetInviteCount(Options ? Options->LocalUserId : nullptr) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetInviteIdByIndex(EOS_HLobby Handle, const EOS_Lobby_GetInviteIdByIndexOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->GetInviteIdByIndex(Options->LocalUserId, Options->Index, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CreateLobbySearch(EOS_HLobby Handle, const EOS_Lobby_CreateLobbySearchOptions* Options, EOS_HLobbySearch* OutLobbySearchHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	return I ? I->CreateLobbySearch(Options, OutLobbySearchHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandleByInviteId(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopyLobbyDetailsHandleByInviteId(Options->InviteId, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	// Resolves a UiEventId minted when the user chose "join" on a discovered
	// lobby in the overlay (BeginJoinFromOverlay / JoinLobbyAccepted).
	return I->CopyDetailsByUiEventId(Options->UiEventId, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandle(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	return I ? I->CopyLobbyDetailsHandle(Options, OutLobbyDetailsHandle) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetRTCRoomName(EOS_HLobby Handle, const EOS_Lobby_GetRTCRoomNameOptions* Options, char* OutBuffer, uint32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	return I ? I->GetRTCRoomName(Options, OutBuffer, InOutBufferLength) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_IsRTCRoomConnected(EOS_HLobby Handle, const EOS_Lobby_IsRTCRoomConnectedOptions* Options, EOS_Bool* bOutIsConnected)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->LobbyId == nullptr || bOutIsConnected == nullptr)
		return EOS_EResult::EOS_InvalidParameters;
	// The RTC shim's pump owns room-connection truth; it adopts the room on the
	// first Tick after the lobby join, so this flips true one Tick later --
	// consistent with the RTCRoomConnectionChanged notification the game sees.
	*bOutIsConnected = I->Owner().RTC().IsInRoom(std::string("rtc_") + Options->LobbyId)
		? EOS_TRUE : EOS_FALSE;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetConnectString(EOS_HLobby Handle, const EOS_Lobby_GetConnectStringOptions* Options, char* OutBuffer, uint32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* I = As<LobbyInterface>(Handle);
	return I ? I->GetConnectString(Options, OutBuffer, InOutBufferLength) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_ParseConnectString(EOS_HLobby Handle, const EOS_Lobby_ParseConnectStringOptions* Options, char* OutBuffer, uint32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || Options->ConnectString == nullptr || InOutBufferLength == nullptr)
		return EOS_EResult::EOS_InvalidParameters;
	// Reverse of GetConnectString: strip the "eosemu:lobby:" prefix.
	const char* Prefix = "eosemu:lobby:";
	const char* Str = Options->ConnectString;
	const char* LobbyId = std::strncmp(Str, Prefix, std::strlen(Prefix)) == 0 ? Str + std::strlen(Prefix) : Str;
	const uint32_t Required = static_cast<uint32_t>(std::strlen(LobbyId)) + 1;
	if (OutBuffer == nullptr || *InOutBufferLength < Required) { *InOutBufferLength = Required; return EOS_EResult::EOS_LimitExceeded; }
	std::memcpy(OutBuffer, LobbyId, Required);
	*InOutBufferLength = Required;
	return EOS_EResult::EOS_Success;
}

// ---------------------------------------------------------------------------
// Notification registration -- all route through the interface's registries.
// ---------------------------------------------------------------------------

#define EOSEMU_LOBBY_ADDNOTIFY(FnName, Registry, CbType)                                        \
	EOS_DECLARE_FUNC(EOS_NotificationId) FnName(EOS_HLobby Handle, const void* Options, void* ClientData, const CbType NotificationFn) \
	{ (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->Registry.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyUpdateReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyUpdateReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->UpdateReceived_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyUpdateReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->UpdateReceived_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberUpdateReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->MemberUpdateReceived_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->MemberUpdateReceived_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyMemberStatusReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberStatusReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->MemberStatusReceived_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->MemberStatusReceived_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteReceivedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->InviteReceived_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->InviteReceived_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteAccepted(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->InviteAccepted_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteAccepted(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->InviteAccepted_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteRejected(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteRejectedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteRejectedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->InviteRejected_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteRejected(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->InviteRejected_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyJoinLobbyAccepted(EOS_HLobby Handle, const EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyAcceptedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->JoinAccepted_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyJoinLobbyAccepted(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->JoinAccepted_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLeaveLobbyRequested(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLeaveLobbyRequestedOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveLobbyRequestedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->LeaveRequested_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLeaveLobbyRequested(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->LeaveRequested_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyRTCRoomConnectionChanged(EOS_HLobby Handle, const EOS_Lobby_AddNotifyRTCRoomConnectionChangedOptions* Options, void* ClientData, const EOS_Lobby_OnRTCRoomConnectionChangedCallback NotificationFn)
{
	EOSEMU_API_TRACE(); (void)Options; auto* I = As<LobbyInterface>(Handle); return I ? I->RTCRoomConn_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID; }
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyRTCRoomConnectionChanged(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); if (auto* I = As<LobbyInterface>(Handle)) I->RTCRoomConn_.Remove(InId); }

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifySendLobbyNativeInviteRequested(EOS_HLobby Handle, const EOS_Lobby_AddNotifySendLobbyNativeInviteRequestedOptions* Options, void* ClientData, const EOS_Lobby_OnSendLobbyNativeInviteRequestedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options; (void)ClientData; (void)NotificationFn;
	// Native (platform overlay) invite path never fires in the LAN model.
	return EOS_INVALID_NOTIFICATIONID;
}
EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifySendLobbyNativeInviteRequested(EOS_HLobby Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE(); (void)Handle; (void)InId; }

// ---------------------------------------------------------------------------
// EOS_LobbyModification mutators
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_LobbyModification_Release(EOS_HLobbyModification LobbyModificationHandle)
{
	EOSEMU_API_TRACE();
	delete AsMod(LobbyModificationHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetBucketId(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetBucketIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->BucketId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetBucket = true; M->Bucket = Options->BucketId;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetPermissionLevel(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetPermissionLevelOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetPerm = true; M->Perm = Options->PermissionLevel;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetMaxMembers(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetMaxMembersOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetMax = true; M->Max = Options->MaxMembers;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetInvitesAllowed(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetInvitesAllowedOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetInvites = true; M->Invites = Options->bInvitesAllowed;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_AddAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_AddAttributeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->Attribute == nullptr || Options->Attribute->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	LobbyAttr A = AttrFromData(Options->Attribute, Options->Visibility);
	EOSEMU_LOG(Lobby, EOS_ELogLevel::EOS_LOG_VeryVerbose, "AddAttribute lobby key='%s' %s vis=%d", Options->Attribute->Key, AttrDesc(A.Value).c_str(), (int)Options->Visibility);
	M->AddAttrs.emplace_back(Options->Attribute->Key, A);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_RemoveAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_RemoveAttributeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->RemoveAttrs.push_back(Options->Key);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_AddMemberAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_AddMemberAttributeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->Attribute == nullptr || Options->Attribute->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	LobbyAttr A = AttrFromData(Options->Attribute, Options->Visibility);
	EOSEMU_LOG(Lobby, EOS_ELogLevel::EOS_LOG_VeryVerbose, "AddMemberAttribute key='%s' %s vis=%d", Options->Attribute->Key, AttrDesc(A.Value).c_str(), (int)Options->Visibility);
	M->AddMemberAttrs.emplace_back(Options->Attribute->Key, A);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_RemoveMemberAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_RemoveMemberAttributeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr || Options->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->RemoveMemberAttrs.push_back(Options->Key);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetAllowedPlatformIds(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetAllowedPlatformIdsOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* M = AsMod(Handle);
	if (M == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	M->bSetPlatforms = true;
	M->Platforms.clear();
	for (uint32_t i = 0; Options->AllowedPlatformIds != nullptr && i < Options->AllowedPlatformIdsCount; ++i)
		M->Platforms.push_back(Options->AllowedPlatformIds[i]);
	return EOS_EResult::EOS_Success;
}

// ---------------------------------------------------------------------------
// EOS_LobbyDetails accessors
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_Release(EOS_HLobbyDetails LobbyHandle)
{
	EOSEMU_API_TRACE();
	delete AsDetails(LobbyHandle);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_LobbyDetails_GetLobbyOwner(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetLobbyOwnerOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* D = AsDetails(Handle);
	return D ? Ids::InternProduct(D->Record.OwnerProductId) : nullptr;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyInfo(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyInfoOptions* Options, EOS_LobbyDetails_Info** OutLobbyDetailsInfo)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* D = AsDetails(Handle);
	if (D == nullptr || OutLobbyDetailsInfo == nullptr) return EOS_EResult::EOS_InvalidParameters;
	const LobbyRecord& Rec = D->Record;
	EOS_LobbyDetails_Info* Info = AllocApi<EOS_LobbyDetails_Info>();
	Info->ApiVersion = EOS_LOBBYDETAILS_INFO_API_LATEST;
	Info->LobbyId = AttachString(Info, Rec.LobbyId.c_str());
	Info->LobbyOwnerUserId = Ids::InternProduct(Rec.OwnerProductId);
	Info->PermissionLevel = Rec.Permission;
	Info->AvailableSlots = Rec.AvailableSlots();
	Info->MaxMembers = Rec.MaxMembers;
	Info->bAllowInvites = Rec.bAllowInvites;
	Info->BucketId = AttachString(Info, Rec.BucketId.c_str());
	Info->bAllowHostMigration = Rec.bAllowHostMigration;
	Info->bRTCRoomEnabled = Rec.bRTCRoomEnabled;
	Info->bAllowJoinById = Rec.bAllowJoinById;
	Info->bRejoinAfterKickRequiresInvite = EOS_FALSE;
	Info->bPresenceEnabled = Rec.bPresenceEnabled;
	if (!Rec.AllowedPlatformIds.empty())
	{
		uint32_t* Arr = AttachArray<uint32_t>(Info, Rec.AllowedPlatformIds.size());
		for (size_t i = 0; i < Rec.AllowedPlatformIds.size(); ++i) Arr[i] = Rec.AllowedPlatformIds[i];
		Info->AllowedPlatformIds = Arr;
		Info->AllowedPlatformIdsCount = static_cast<uint32_t>(Rec.AllowedPlatformIds.size());
	}
	*OutLobbyDetailsInfo = Info;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_Info_Release(EOS_LobbyDetails_Info* LobbyDetailsInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(LobbyDetailsInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberInfo(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberInfoOptions* Options, EOS_LobbyDetails_MemberInfo** OutLobbyDetailsMemberInfo)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->TargetUserId == nullptr || OutLobbyDetailsMemberInfo == nullptr) return EOS_EResult::EOS_InvalidParameters;
	const std::string Pid = Ids::ProductString(Options->TargetUserId);
	const LobbyMemberRec* M = D->Record.FindMember(Pid);
	if (M == nullptr) return EOS_EResult::EOS_NotFound;
	EOS_LobbyDetails_MemberInfo* Info = AllocApi<EOS_LobbyDetails_MemberInfo>();
	Info->ApiVersion = EOS_LOBBYDETAILS_MEMBERINFO_API_LATEST;
	Info->UserId = Options->TargetUserId;
	Info->Platform = M->Platform;
	Info->bAllowsCrossplay = EOS_TRUE;
	*OutLobbyDetailsMemberInfo = Info;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_MemberInfo_Release(EOS_LobbyDetails_MemberInfo* LobbyDetailsMemberInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(LobbyDetailsMemberInfo);
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetAttributeCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetAttributeCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* D = AsDetails(Handle);
	return D ? static_cast<uint32_t>(D->Record.Attributes.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyAttributeByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyAttributeByIndexOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || OutAttribute == nullptr) return EOS_EResult::EOS_InvalidParameters;
	if (Options->AttrIndex >= D->Record.Attributes.size()) return EOS_EResult::EOS_NotFound;
	const auto& A = D->Record.Attributes[Options->AttrIndex];
	EOSEMU_LOG(Lobby, EOS_ELogLevel::EOS_LOG_VeryVerbose, "CopyAttributeByIndex[%u] lobby='%s' key='%s' %s", Options->AttrIndex, D->Record.LobbyId.c_str(), A.first.c_str(), AttrDesc(A.second.Value).c_str());
	*OutAttribute = MakeAttribute(A.first, A.second);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyAttributeByKey(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyAttributeByKeyOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->AttrKey == nullptr || OutAttribute == nullptr) return EOS_EResult::EOS_InvalidParameters;
	for (const auto& A : D->Record.Attributes)
		if (A.first == Options->AttrKey) { *OutAttribute = MakeAttribute(A.first, A.second); return EOS_EResult::EOS_Success; }
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetMemberCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* D = AsDetails(Handle);
	return D ? static_cast<uint32_t>(D->Record.Members.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_LobbyDetails_GetMemberByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberByIndexOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->MemberIndex >= D->Record.Members.size()) return nullptr;
	return Ids::InternProduct(D->Record.Members[Options->MemberIndex].ProductId);
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetMemberAttributeCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberAttributeCountOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->TargetUserId == nullptr) return 0;
	const LobbyMemberRec* M = D->Record.FindMember(Ids::ProductString(Options->TargetUserId));
	return M ? static_cast<uint32_t>(M->Attributes.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberAttributeByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberAttributeByIndexOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->TargetUserId == nullptr || OutAttribute == nullptr) return EOS_EResult::EOS_InvalidParameters;
	const LobbyMemberRec* M = D->Record.FindMember(Ids::ProductString(Options->TargetUserId));
	if (M == nullptr || Options->AttrIndex >= M->Attributes.size()) return EOS_EResult::EOS_NotFound;
	const auto& A = M->Attributes[Options->AttrIndex];
	EOSEMU_LOG(Lobby, EOS_ELogLevel::EOS_LOG_VeryVerbose, "CopyMemberAttributeByIndex[%u] member=%s key='%s' %s", Options->AttrIndex, Ids::ProductString(Options->TargetUserId).c_str(), A.first.c_str(), AttrDesc(A.second.Value).c_str());
	*OutAttribute = MakeAttribute(A.first, A.second);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberAttributeByKey(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberAttributeByKeyOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
	EOSEMU_API_TRACE();
	auto* D = AsDetails(Handle);
	if (D == nullptr || Options == nullptr || Options->TargetUserId == nullptr || Options->AttrKey == nullptr || OutAttribute == nullptr) return EOS_EResult::EOS_InvalidParameters;
	const LobbyMemberRec* M = D->Record.FindMember(Ids::ProductString(Options->TargetUserId));
	if (M == nullptr) return EOS_EResult::EOS_NotFound;
	for (const auto& A : M->Attributes)
		if (A.first == Options->AttrKey) { *OutAttribute = MakeAttribute(A.first, A.second); return EOS_EResult::EOS_Success; }
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_Attribute_Release(EOS_Lobby_Attribute* LobbyAttribute)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(LobbyAttribute);
}

// ---------------------------------------------------------------------------
// EOS_LobbySearch
// ---------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_LobbySearch_Release(EOS_HLobbySearch LobbySearchHandle)
{
	EOSEMU_API_TRACE();
	RetireSearch(AsSearch(LobbySearchHandle));
}

EOS_DECLARE_FUNC(void) EOS_LobbySearch_Find(EOS_HLobbySearch Handle, const EOS_LobbySearch_FindOptions* Options, void* ClientData, const EOS_LobbySearch_OnFindCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* Search = AsSearch(Handle);
	LobbyInterface* I = Iface();
	Platform* P = CurrentPlatform();

	const bool Ok = (Search != nullptr && I != nullptr);
	// The interface owns Known_; ask it to fill the result set.
	if (Ok) I->CollectSearchResults(*Search);

	if (CompletionDelegate == nullptr) return;
	const EOS_EResult Result = Ok ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters;
	auto Fire = [CompletionDelegate, ClientData, Result]
	{
		EOS_LobbySearch_FindCallbackInfo Info = {};
		Info.ResultCode = Result;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	};
	// Fire the completion after a short backend-latency gap, NOT on the next
	// Tick. The real SDK's Find completes ~200ms after the call (verified with
	// EOSTracer against a shipped game's host flow); it copies the result only
	// inside this completion, and delivering it too soon races its coroutine and
	// hands CopySearchResultByIndex a null handle -> hosting fails. Never invoke
	// with a null Data and never drop the completion (a caller gating on
	// IsOperationComplete would wait forever); with no platform, inline is
	// least-bad.
	if (P) P->Dispatch().PostAfter(kSearchLatencyMs, Fire); else Fire();
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetLobbyId(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetLobbyIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->LobbyId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	S->ByLobbyId = Options->LobbyId;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetTargetUserId(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetTargetUserIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->TargetUserId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	S->ByTargetUser = Ids::ProductString(Options->TargetUserId);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetParameter(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetParameterOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->Parameter == nullptr || Options->Parameter->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	LobbySearchObj::Param P;
	P.Key = Options->Parameter->Key;
	P.Value = ReadAttr(Options->Parameter->ValueType, Options->Parameter->Value.AsInt64, Options->Parameter->Value.AsDouble, Options->Parameter->Value.AsBool, Options->Parameter->Value.AsUtf8);
	P.Op = Options->ComparisonOp;
	// Replace an existing parameter with the same key.
	for (auto& Existing : S->Params) if (Existing.Key == P.Key) { Existing = P; return EOS_EResult::EOS_Success; }
	S->Params.push_back(P);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_RemoveParameter(EOS_HLobbySearch Handle, const EOS_LobbySearch_RemoveParameterOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->Key == nullptr) return EOS_EResult::EOS_InvalidParameters;
	for (auto It = S->Params.begin(); It != S->Params.end(); ++It)
		if (It->Key == Options->Key) { S->Params.erase(It); break; }
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetMaxResults(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetMaxResultsOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || Options->MaxResults == 0) return EOS_EResult::EOS_InvalidParameters;
	S->MaxResults = Options->MaxResults;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbySearch_GetSearchResultCount(EOS_HLobbySearch Handle, const EOS_LobbySearch_GetSearchResultCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* S = AsSearch(Handle);
	return S ? static_cast<uint32_t>(S->Results.size()) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_CopySearchResultByIndex(EOS_HLobbySearch Handle, const EOS_LobbySearch_CopySearchResultByIndexOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
	EOSEMU_API_TRACE();
	auto* S = AsSearch(Handle);
	if (S == nullptr || Options == nullptr || OutLobbyDetailsHandle == nullptr) return EOS_EResult::EOS_InvalidParameters;
	if (Options->LobbyIndex >= S->Results.size()) return EOS_EResult::EOS_NotFound;
	auto* Details = new LobbyDetailsObj();
	Details->Record = S->Results[Options->LobbyIndex];
	*OutLobbyDetailsHandle = reinterpret_cast<EOS_HLobbyDetails>(Details);
	return EOS_EResult::EOS_Success;
}
