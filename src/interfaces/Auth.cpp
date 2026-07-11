//
// Auth interface -- shimmed to always succeed.
//
// The samples default to DevAuth login (AuthDialogs.cpp:1350), so Login must
// succeed for EOS_LCT_Developer regardless of Id/Token. We succeed for every
// credential type: a LAN emulator has no real backend to reject against, and a
// logged-in user is exactly what every consumer wants next. The one gate to
// honour is FAuthentication::ConnectLogin (Authentication.cpp:340), which only
// proceeds if CopyUserAuthToken yields EOS_Success with a non-null AccessToken.
//

#include "interfaces/Auth.h"

#include "core/Identity.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/Platform.h"

namespace EOSEmu
{
	AuthInterface::AuthInterface(Platform& Owner) : InterfaceBase(Owner) {}

	EOS_EpicAccountId AuthInterface::LocalUser() const
	{
		return Platform_.LocalIdentity().EpicAccountId();
	}

	EOS_EpicAccountId AuthInterface::LoggedInByIndex(int32_t Index) const
	{
		if (bLoggedIn_ && Index == 0)
		{
			return LocalUser();
		}
		return nullptr;
	}

	EOS_ELoginStatus AuthInterface::LoginStatus(EOS_EpicAccountId User) const
	{
		if (bLoggedIn_ && User == LocalUser())
		{
			return EOS_ELoginStatus::EOS_LS_LoggedIn;
		}
		return EOS_ELoginStatus::EOS_LS_NotLoggedIn;
	}

	EOS_EpicAccountId AuthInterface::SelectedAccountId() const
	{
		return bLoggedIn_ ? LocalUser() : nullptr;
	}

	void AuthInterface::Login(const EOS_Auth_LoginOptions* Options, void* ClientData, EOS_Auth_OnLoginCallback Cb)
	{
		EOS_ELoginCredentialType Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
		if (Options != nullptr && Options->ApiVersion >= 1 && Options->Credentials != nullptr)
		{
			const EOS_Auth_Credentials* Creds = Options->Credentials;
			Type = Creds->Type;
			// An ExternalAuth login with a Steam session ticket carries the real
			// SteamID64 (ExternalType arrived at Credentials v3). Recover it so
			// Connect's ExternalAccountInfo reports the id Steam itself uses.
			if (Type == EOS_ELoginCredentialType::EOS_LCT_ExternalAuth &&
				HasField(Creds->ApiVersion, 3) &&
				Creds->ExternalType == EOS_EExternalCredentialType::EOS_ECT_STEAM_SESSION_TICKET &&
				Creds->Token != nullptr)
			{
				Platform_.NoteSteamSessionTicket(Creds->Token);
			}
		}
		EOSEMU_INFO(Auth, "Auth_Login: credential type %d -> shim success",
			static_cast<int>(Type));

		const bool WasLoggedIn = bLoggedIn_;
		bLoggedIn_ = true;
		EOS_EpicAccountId User = LocalUser();

		if (Cb == nullptr)
		{
			return;
		}
		// Fire on the Tick thread, never inline (CLAUDE.md callback rule).
		Platform_.Dispatch().Post([Cb, ClientData, User, WasLoggedIn, this]
		{
			EOS_Auth_LoginCallbackInfo Info = {};
			Info.ResultCode = EOS_EResult::EOS_Success;
			Info.ClientData = ClientData;
			Info.LocalUserId = User;
			Info.SelectedAccountId = User;
			Cb(&Info);

			if (!WasLoggedIn)
			{
				FireStatusChanged(EOS_ELoginStatus::EOS_LS_NotLoggedIn, EOS_ELoginStatus::EOS_LS_LoggedIn);
			}
		});
	}

	void AuthInterface::Logout(const EOS_Auth_LogoutOptions* Options, void* ClientData, EOS_Auth_OnLogoutCallback Cb)
	{
		EOS_EpicAccountId User = (Options != nullptr) ? Options->LocalUserId : LocalUser();
		const bool WasLoggedIn = bLoggedIn_;
		bLoggedIn_ = false;

		Platform_.Dispatch().Post([Cb, ClientData, User, WasLoggedIn, this]
		{
			if (Cb != nullptr)
			{
				EOS_Auth_LogoutCallbackInfo Info = {};
				Info.ResultCode = EOS_EResult::EOS_Success;
				Info.ClientData = ClientData;
				Info.LocalUserId = User;
				Cb(&Info);
			}
			if (WasLoggedIn)
			{
				FireStatusChanged(EOS_ELoginStatus::EOS_LS_LoggedIn, EOS_ELoginStatus::EOS_LS_NotLoggedIn);
			}
		});
	}

	void AuthInterface::FireStatusChanged(EOS_ELoginStatus Prev, EOS_ELoginStatus Now)
	{
		EOS_EpicAccountId User = LocalUser();
		for (const auto& Entry : StatusChanged_.Snapshot())
		{
			EOS_Auth_LoginStatusChangedCallbackInfo Info = {};
			Info.ClientData = Entry.ClientData;
			Info.LocalUserId = User;
			Info.PrevStatus = Prev;
			Info.CurrentStatus = Now;
			Entry.Fn(&Info);
		}
	}

	EOS_EResult AuthInterface::CopyUserAuthToken(EOS_EpicAccountId LocalUserId, EOS_Auth_Token** Out)
	{
		if (Out == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		*Out = nullptr;
		if (!bLoggedIn_)
		{
			return EOS_EResult::EOS_NotFound;
		}

		EOS_Auth_Token* Token = AllocApi<EOS_Auth_Token>();
		Token->ApiVersion = EOS_AUTH_TOKEN_API_LATEST;
		Token->App = AttachString(Token, "EOSEmu");
		Token->ClientId = AttachString(Token, "EOSEmuClient");
		Token->AccountId = (LocalUserId != nullptr) ? LocalUserId : LocalUser();
		// The gate at Authentication.cpp:340 only checks this is non-null.
		Token->AccessToken = AttachString(Token, "eosemu-access-token");
		Token->ExpiresIn = 3600.0;
		Token->ExpiresAt = AttachString(Token, "2099-12-31T23:59:59.999Z");
		Token->AuthType = EOS_EAuthTokenType::EOS_ATT_User;
		Token->RefreshToken = AttachString(Token, "eosemu-refresh-token");
		Token->RefreshExpiresIn = 86400.0;
		Token->RefreshExpiresAt = AttachString(Token, "2099-12-31T23:59:59.999Z");
		*Out = Token;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult AuthInterface::CopyIdToken(EOS_EpicAccountId AccountId, EOS_Auth_IdToken** Out)
	{
		if (Out == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		*Out = nullptr;
		if (!bLoggedIn_)
		{
			return EOS_EResult::EOS_NotFound;
		}

		EOS_Auth_IdToken* Token = AllocApi<EOS_Auth_IdToken>();
		Token->ApiVersion = EOS_AUTH_IDTOKEN_API_LATEST;
		Token->AccountId = (AccountId != nullptr) ? AccountId : LocalUser();
		// A structurally valid but locally-minted JWT-shaped string.
		Token->JsonWebToken = AttachString(Token,
			"eyJhbGciOiJub25lIn0.eyJpc3MiOiJlb3NlbXUifQ.");
		*Out = Token;
		return EOS_EResult::EOS_Success;
	}
}

// ----------------------------------------------------------------------------
// Exported entry points.
// ----------------------------------------------------------------------------

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Auth_Login(EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options, void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<AuthInterface>(Handle))
	{
		I->Login(Options, ClientData, CompletionDelegate);
	}
}

EOS_DECLARE_FUNC(void) EOS_Auth_Logout(EOS_HAuth Handle, const EOS_Auth_LogoutOptions* Options, void* ClientData, const EOS_Auth_OnLogoutCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<AuthInterface>(Handle))
	{
		I->Logout(Options, ClientData, CompletionDelegate);
	}
}

EOS_DECLARE_FUNC(int32_t) EOS_Auth_GetLoggedInAccountsCount(EOS_HAuth Handle)
{
	EOSEMU_API_TRACE();
	auto* I = As<AuthInterface>(Handle);
	return I ? I->LoggedInCount() : 0;
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Auth_GetLoggedInAccountByIndex(EOS_HAuth Handle, int32_t Index)
{
	EOSEMU_API_TRACE();
	auto* I = As<AuthInterface>(Handle);
	return I ? I->LoggedInByIndex(Index) : nullptr;
}

EOS_DECLARE_FUNC(EOS_ELoginStatus) EOS_Auth_GetLoginStatus(EOS_HAuth Handle, EOS_EpicAccountId LocalUserId)
{
	EOSEMU_API_TRACE();
	auto* I = As<AuthInterface>(Handle);
	return I ? I->LoginStatus(LocalUserId) : EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_CopyUserAuthToken(EOS_HAuth Handle, const EOS_Auth_CopyUserAuthTokenOptions* Options, EOS_EpicAccountId LocalUserId, EOS_Auth_Token** OutUserAuthToken)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<AuthInterface>(Handle);
	return I ? I->CopyUserAuthToken(LocalUserId, OutUserAuthToken) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_CopyIdToken(EOS_HAuth Handle, const EOS_Auth_CopyIdTokenOptions* Options, EOS_Auth_IdToken** OutIdToken)
{
	EOSEMU_API_TRACE();
	auto* I = As<AuthInterface>(Handle);
	EOS_EpicAccountId Account = (Options != nullptr) ? Options->AccountId : nullptr;
	return I ? I->CopyIdToken(Account, OutIdToken) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_GetSelectedAccountId(EOS_HAuth Handle, const EOS_EpicAccountId LocalUserId, EOS_EpicAccountId* OutSelectedAccountId)
{
	EOSEMU_API_TRACE();
	if (OutSelectedAccountId == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	auto* I = As<AuthInterface>(Handle);
	if (I == nullptr || !I->IsLoggedIn())
	{
		return EOS_EResult::EOS_InvalidAuth;
	}
	(void)LocalUserId;
	*OutSelectedAccountId = I->SelectedAccountId();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Auth_GetMergedAccountsCount(EOS_HAuth Handle, const EOS_EpicAccountId LocalUserId)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)LocalUserId;
	return 0;
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Auth_GetMergedAccountByIndex(EOS_HAuth Handle, const EOS_EpicAccountId LocalUserId, const uint32_t Index)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)LocalUserId; (void)Index;
	return nullptr;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Auth_AddNotifyLoginStatusChanged(EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback Notification)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<AuthInterface>(Handle);
	return I ? I->AddNotifyLoginStatusChanged(Notification, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Auth_RemoveNotifyLoginStatusChanged(EOS_HAuth Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<AuthInterface>(Handle))
	{
		I->RemoveNotifyLoginStatusChanged(InId);
	}
}

EOS_DECLARE_FUNC(void) EOS_Auth_Token_Release(EOS_Auth_Token* AuthToken)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(AuthToken);
}

// ----------------------------------------------------------------------------
// Remaining Auth surface -- plausible successes for a shimmed backend.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_Auth_LinkAccount(EOS_HAuth Handle, const EOS_Auth_LinkAccountOptions* Options, void* ClientData, const EOS_Auth_OnLinkAccountCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<AuthInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr)
	{
		return;
	}
	EOS_EpicAccountId User = I->LocalUser();
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, User]
	{
		EOS_Auth_LinkAccountCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = User;
		Info.SelectedAccountId = User;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Auth_DeletePersistentAuth(EOS_HAuth Handle, const EOS_Auth_DeletePersistentAuthOptions* Options, void* ClientData, const EOS_Auth_OnDeletePersistentAuthCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<AuthInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr)
	{
		return;
	}
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_Auth_DeletePersistentAuthCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Auth_VerifyUserAuth(EOS_HAuth Handle, const EOS_Auth_VerifyUserAuthOptions* Options, void* ClientData, const EOS_Auth_OnVerifyUserAuthCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<AuthInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr)
	{
		return;
	}
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_Auth_VerifyUserAuthCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Auth_QueryIdToken(EOS_HAuth Handle, const EOS_Auth_QueryIdTokenOptions* Options, void* ClientData, const EOS_Auth_OnQueryIdTokenCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<AuthInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr)
	{
		return;
	}
	EOS_EpicAccountId Local = (Options != nullptr) ? Options->LocalUserId : I->LocalUser();
	EOS_EpicAccountId Target = (Options != nullptr) ? Options->TargetAccountId : I->LocalUser();
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_Auth_QueryIdTokenCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetAccountId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Auth_VerifyIdToken(EOS_HAuth Handle, const EOS_Auth_VerifyIdTokenOptions* Options, void* ClientData, const EOS_Auth_OnVerifyIdTokenCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<AuthInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr)
	{
		return;
	}
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_Auth_VerifyIdTokenCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Auth_IdToken_Release(EOS_Auth_IdToken* IdToken)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(IdToken);
}
