#pragma once

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_connect_types.h"

#include <string>

namespace EOSEmu
{
	// Connect maps external credentials to a Product User ID. Shimmed: the local
	// user always resolves to the deterministic Product User ID from Identity,
	// and login succeeds without contacting a backend.
	class ConnectInterface : public InterfaceBase
	{
	public:
		explicit ConnectInterface(Platform& Owner);

		void Login(const EOS_Connect_LoginOptions* Options, void* ClientData, EOS_Connect_OnLoginCallback Cb);
		void CreateUser(const EOS_Connect_CreateUserOptions* Options, void* ClientData, EOS_Connect_OnCreateUserCallback Cb);
		EOS_ELoginStatus LoginStatus(EOS_ProductUserId User) const;

		int32_t LoggedInUsersCount() const { return bLoggedIn_ ? 1 : 0; }
		EOS_ProductUserId LoggedInUserByIndex(int32_t Index) const;
		EOS_ProductUserId LocalUser() const;

		EOS_NotificationId AddNotifyLoginStatusChanged(EOS_Connect_OnLoginStatusChangedCallback Cb, void* ClientData)
		{
			return StatusChanged_.Add(Cb, ClientData);
		}
		void RemoveNotifyLoginStatusChanged(EOS_NotificationId Id) { StatusChanged_.Remove(Id); }
		EOS_NotificationId AddNotifyAuthExpiration(EOS_Connect_OnAuthExpirationCallback Cb, void* ClientData)
		{
			return AuthExpiration_.Add(Cb, ClientData);
		}
		void RemoveNotifyAuthExpiration(EOS_NotificationId Id) { AuthExpiration_.Remove(Id); }

		bool IsLoggedIn() const { return bLoggedIn_; }

		// The external identity provider the local user logged in with, captured
		// from EOS_Connect_Login's credential type (e.g. a Steam session ticket
		// -> EOS_EAT_STEAM). Reported back through the ExternalAccountInfo family
		// so a consumer sees the same provider it authenticated against, rather
		// than a hardcoded Epic account. DisplayName is whatever the caller passed
		// in UserLoginInfo (empty if none -- callers fall back to the local name).
		EOS_EExternalAccountType ExternalAccountType() const { return ExternalType_; }
		const std::string& ExternalDisplayName() const { return ExternalDisplayName_; }

	private:
		bool bLoggedIn_ = false;
		EOS_EExternalAccountType ExternalType_ = EOS_EExternalAccountType::EOS_EAT_EPIC;
		std::string ExternalDisplayName_;
		NotifyRegistry<EOS_Connect_OnLoginStatusChangedCallback> StatusChanged_;
		NotifyRegistry<EOS_Connect_OnAuthExpirationCallback> AuthExpiration_;
	};
}
