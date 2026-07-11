#pragma once

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_auth_types.h"

namespace EOSEmu
{
	// Auth is shimmed to always succeed. It tracks a single local Epic account
	// (there is one local user) and hands out a synthetic auth/ID token that the
	// Connect login flow will accept. See CLAUDE.md, "Shim to success".
	class AuthInterface : public InterfaceBase
	{
	public:
		explicit AuthInterface(Platform& Owner);

		void Login(const EOS_Auth_LoginOptions* Options, void* ClientData, EOS_Auth_OnLoginCallback Cb);
		void Logout(const EOS_Auth_LogoutOptions* Options, void* ClientData, EOS_Auth_OnLogoutCallback Cb);

		EOS_EResult CopyUserAuthToken(EOS_EpicAccountId LocalUserId, EOS_Auth_Token** Out);
		EOS_EResult CopyIdToken(EOS_EpicAccountId AccountId, EOS_Auth_IdToken** Out);

		int32_t LoggedInCount() const { return bLoggedIn_ ? 1 : 0; }
		EOS_EpicAccountId LoggedInByIndex(int32_t Index) const;
		EOS_ELoginStatus LoginStatus(EOS_EpicAccountId User) const;
		EOS_EpicAccountId SelectedAccountId() const;

		EOS_NotificationId AddNotifyLoginStatusChanged(EOS_Auth_OnLoginStatusChangedCallback Cb, void* ClientData)
		{
			return StatusChanged_.Add(Cb, ClientData);
		}
		void RemoveNotifyLoginStatusChanged(EOS_NotificationId Id) { StatusChanged_.Remove(Id); }

		bool IsLoggedIn() const { return bLoggedIn_; }
		EOS_EpicAccountId LocalUser() const;

	private:
		void FireStatusChanged(EOS_ELoginStatus Prev, EOS_ELoginStatus Now);

		bool bLoggedIn_ = false;
		NotifyRegistry<EOS_Auth_OnLoginStatusChangedCallback> StatusChanged_;
	};
}
