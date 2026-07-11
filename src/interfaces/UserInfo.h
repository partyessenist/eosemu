#pragma once

#include <string>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_userinfo_types.h"

namespace EOSEmu
{
	// UserInfo resolves Epic Account IDs to display names, sourced from the LAN
	// peer directory (each peer announces its display name in Hello). Queries
	// succeed immediately since the data is already local.
	class UserInfoInterface : public InterfaceBase
	{
	public:
		explicit UserInfoInterface(Platform& Owner) : InterfaceBase(Owner) {}

		void QueryUserInfo(EOS_EpicAccountId Local, EOS_EpicAccountId Target, void* ClientData, EOS_UserInfo_OnQueryUserInfoCallback Cb);
		EOS_EResult CopyUserInfo(EOS_EpicAccountId Target, EOS_UserInfo** Out);
		EOS_EResult CopyBestDisplayName(EOS_EpicAccountId Target, EOS_UserInfo_BestDisplayName** Out);

		/// Display name for an Epic account, from the peer directory or the local
		/// identity. Empty if unknown.
		std::string DisplayNameFor(EOS_EpicAccountId Account);
	};
}
