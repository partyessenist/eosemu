#pragma once

#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_friends_types.h"

namespace EOSEmu
{
	// Friends are the LAN peers discovered via Hello. Everyone on the LAN is a
	// friend by construction, so the list is the peer directory and every status
	// query answers EOS_FS_Friends.
	class FriendsInterface : public InterfaceBase
	{
	public:
		explicit FriendsInterface(Platform& Owner) : InterfaceBase(Owner) {}

		int32_t GetFriendsCount();
		EOS_EpicAccountId GetFriendAtIndex(int32_t Index);
		EOS_EFriendsStatus GetStatus(EOS_EpicAccountId Target);

		EOS_NotificationId AddNotifyFriendsUpdate(EOS_Friends_OnFriendsUpdateCallback Cb, void* ClientData) { return FriendsUpdate_.Add(Cb, ClientData); }
		void RemoveNotifyFriendsUpdate(EOS_NotificationId Id) { FriendsUpdate_.Remove(Id); }
		EOS_NotificationId AddNotifyBlockedUsersUpdate(EOS_Friends_OnBlockedUsersUpdateCallback Cb, void* ClientData) { return BlockedUpdate_.Add(Cb, ClientData); }
		void RemoveNotifyBlockedUsersUpdate(EOS_NotificationId Id) { BlockedUpdate_.Remove(Id); }

	private:
		// Stable, sorted snapshot of friend Epic IDs so GetFriendAtIndex is
		// consistent within a frame.
		std::vector<EOS_EpicAccountId> FriendSnapshot();

		NotifyRegistry<EOS_Friends_OnFriendsUpdateCallback> FriendsUpdate_;
		NotifyRegistry<EOS_Friends_OnBlockedUsersUpdateCallback> BlockedUpdate_;
	};
}
