//
// Friends interface -- the LAN peer set.
//

#include "interfaces/Friends.h"
#include "core/Logging.h"

#include "core/Ids.h"
#include "core/Peers.h"
#include "core/Platform.h"

#include <algorithm>

namespace EOSEmu
{
	std::vector<EOS_EpicAccountId> FriendsInterface::FriendSnapshot()
	{
		std::vector<PeerInfo> Peers = Platform_.Peers().Snapshot();
		std::vector<std::pair<std::string, EOS_EpicAccountId>> Sorted;
		for (const auto& P : Peers)
		{
			if (P.EpicAccountId != nullptr) Sorted.emplace_back(Ids::EpicString(P.EpicAccountId), P.EpicAccountId);
		}
		std::sort(Sorted.begin(), Sorted.end(), [](const auto& A, const auto& B) { return A.first < B.first; });
		std::vector<EOS_EpicAccountId> Out;
		Out.reserve(Sorted.size());
		for (auto& S : Sorted) Out.push_back(S.second);
		return Out;
	}

	int32_t FriendsInterface::GetFriendsCount()
	{
		return static_cast<int32_t>(FriendSnapshot().size());
	}

	EOS_EpicAccountId FriendsInterface::GetFriendAtIndex(int32_t Index)
	{
		std::vector<EOS_EpicAccountId> Friends = FriendSnapshot();
		if (Index < 0 || Index >= static_cast<int32_t>(Friends.size())) return nullptr;
		return Friends[Index];
	}

	EOS_EFriendsStatus FriendsInterface::GetStatus(EOS_EpicAccountId Target)
	{
		if (Target == nullptr) return EOS_EFriendsStatus::EOS_FS_NotFriends;
		PeerInfo Info;
		if (Platform_.Peers().FindByEpic(Target, Info)) return EOS_EFriendsStatus::EOS_FS_Friends;
		return EOS_EFriendsStatus::EOS_FS_NotFriends;
	}
}

using namespace EOSEmu;

namespace
{
	// Friends invite/accept/reject: LAN peers are already friends, so these are
	// no-op successes that report the target.
	template <typename Info, typename Cb>
	void PostFriendResult(FriendsInterface* I, Cb Callback, void* ClientData, EOS_EpicAccountId Local, EOS_EpicAccountId Target)
	{
		if (I == nullptr || Callback == nullptr) return;
		I->Owner().Dispatch().Post([Callback, ClientData, Local, Target]
		{
			Info Data = {};
			Data.ResultCode = EOS_EResult::EOS_Success;
			Data.ClientData = ClientData;
			Data.LocalUserId = Local;
			Data.TargetUserId = Target;
			Callback(&Data);
		});
	}
}

EOS_DECLARE_FUNC(void) EOS_Friends_QueryFriends(EOS_HFriends Handle, const EOS_Friends_QueryFriendsOptions* Options, void* ClientData, const EOS_Friends_OnQueryFriendsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<FriendsInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local]
	{
		EOS_Friends_QueryFriendsCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Friends_SendInvite(EOS_HFriends Handle, const EOS_Friends_SendInviteOptions* Options, void* ClientData, const EOS_Friends_OnSendInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<FriendsInterface>(Handle);
	PostFriendResult<EOS_Friends_SendInviteCallbackInfo>(I, CompletionDelegate, ClientData,
		Options ? Options->LocalUserId : nullptr, Options ? Options->TargetUserId : nullptr);
}

EOS_DECLARE_FUNC(void) EOS_Friends_AcceptInvite(EOS_HFriends Handle, const EOS_Friends_AcceptInviteOptions* Options, void* ClientData, const EOS_Friends_OnAcceptInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<FriendsInterface>(Handle);
	PostFriendResult<EOS_Friends_AcceptInviteCallbackInfo>(I, CompletionDelegate, ClientData,
		Options ? Options->LocalUserId : nullptr, Options ? Options->TargetUserId : nullptr);
}

EOS_DECLARE_FUNC(void) EOS_Friends_RejectInvite(EOS_HFriends Handle, const EOS_Friends_RejectInviteOptions* Options, void* ClientData, const EOS_Friends_OnRejectInviteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<FriendsInterface>(Handle);
	PostFriendResult<EOS_Friends_RejectInviteCallbackInfo>(I, CompletionDelegate, ClientData,
		Options ? Options->LocalUserId : nullptr, Options ? Options->TargetUserId : nullptr);
}

EOS_DECLARE_FUNC(int32_t) EOS_Friends_GetFriendsCount(EOS_HFriends Handle, const EOS_Friends_GetFriendsCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<FriendsInterface>(Handle);
	return I ? I->GetFriendsCount() : 0;
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Friends_GetFriendAtIndex(EOS_HFriends Handle, const EOS_Friends_GetFriendAtIndexOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<FriendsInterface>(Handle);
	return (I && Options) ? I->GetFriendAtIndex(Options->Index) : nullptr;
}

EOS_DECLARE_FUNC(EOS_EFriendsStatus) EOS_Friends_GetStatus(EOS_HFriends Handle, const EOS_Friends_GetStatusOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<FriendsInterface>(Handle);
	return (I && Options) ? I->GetStatus(Options->TargetUserId) : EOS_EFriendsStatus::EOS_FS_NotFriends;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Friends_AddNotifyFriendsUpdate(EOS_HFriends Handle, const EOS_Friends_AddNotifyFriendsUpdateOptions* Options, void* ClientData, const EOS_Friends_OnFriendsUpdateCallback FriendsUpdateHandler)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<FriendsInterface>(Handle);
	return I ? I->AddNotifyFriendsUpdate(FriendsUpdateHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Friends_RemoveNotifyFriendsUpdate(EOS_HFriends Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<FriendsInterface>(Handle)) I->RemoveNotifyFriendsUpdate(NotificationId);
}

EOS_DECLARE_FUNC(int32_t) EOS_Friends_GetBlockedUsersCount(EOS_HFriends Handle, const EOS_Friends_GetBlockedUsersCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return 0;
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Friends_GetBlockedUserAtIndex(EOS_HFriends Handle, const EOS_Friends_GetBlockedUserAtIndexOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return nullptr;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Friends_AddNotifyBlockedUsersUpdate(EOS_HFriends Handle, const EOS_Friends_AddNotifyBlockedUsersUpdateOptions* Options, void* ClientData, const EOS_Friends_OnBlockedUsersUpdateCallback Callback)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<FriendsInterface>(Handle);
	return I ? I->AddNotifyBlockedUsersUpdate(Callback, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Friends_RemoveNotifyBlockedUsersUpdate(EOS_HFriends Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<FriendsInterface>(Handle)) I->RemoveNotifyBlockedUsersUpdate(NotificationId);
}
