//
// Connect interface -- shimmed to a single deterministic local Product User ID.
//
// EOS_Connect_Login succeeds directly (existing-user path) rather than emitting
// a continuance token, so the sample's ConnectLogin proceeds immediately. The
// InvalidUser -> CreateUser continuation (Authentication.cpp:605) is still
// supported for any consumer that drives it explicitly. Product User ID mapping
// resolves an ID to its own string, which is all a LAN peer needs to address
// another peer. See CLAUDE.md, "Shim to success".
//

#include "interfaces/Connect.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <string>

namespace EOSEmu
{
	namespace
	{
		// Map the external *credential* type a game authenticates with onto the
		// external *account* type reported back through ExternalAccountInfo. The
		// two enums are numbered independently, so this cannot be a cast.
		EOS_EExternalAccountType AccountTypeFromCredential(EOS_EExternalCredentialType Type)
		{
			using CT = EOS_EExternalCredentialType;
			using AT = EOS_EExternalAccountType;
			switch (Type)
			{
			case CT::EOS_ECT_STEAM_APP_TICKET:
			case CT::EOS_ECT_STEAM_SESSION_TICKET:  return AT::EOS_EAT_STEAM;
			case CT::EOS_ECT_PSN_ID_TOKEN:          return AT::EOS_EAT_PSN;
			case CT::EOS_ECT_XBL_XSTS_TOKEN:        return AT::EOS_EAT_XBL;
			case CT::EOS_ECT_DISCORD_ACCESS_TOKEN:  return AT::EOS_EAT_DISCORD;
			case CT::EOS_ECT_GOG_SESSION_TICKET:    return AT::EOS_EAT_GOG;
			case CT::EOS_ECT_NINTENDO_ID_TOKEN:
			case CT::EOS_ECT_NINTENDO_NSA_ID_TOKEN: return AT::EOS_EAT_NINTENDO;
			case CT::EOS_ECT_UPLAY_ACCESS_TOKEN:    return AT::EOS_EAT_UPLAY;
			case CT::EOS_ECT_OPENID_ACCESS_TOKEN:   return AT::EOS_EAT_OPENID;
			case CT::EOS_ECT_APPLE_ID_TOKEN:        return AT::EOS_EAT_APPLE;
			case CT::EOS_ECT_GOOGLE_ID_TOKEN:       return AT::EOS_EAT_GOOGLE;
			case CT::EOS_ECT_OCULUS_USERID_NONCE:   return AT::EOS_EAT_OCULUS;
			case CT::EOS_ECT_ITCHIO_JWT:
			case CT::EOS_ECT_ITCHIO_KEY:            return AT::EOS_EAT_ITCHIO;
			case CT::EOS_ECT_AMAZON_ACCESS_TOKEN:   return AT::EOS_EAT_AMAZON;
			case CT::EOS_ECT_VIVEPORT_USER_TOKEN:   return AT::EOS_EAT_VIVEPORT;
			// EOS_ECT_EPIC / EPIC_ID_TOKEN / DEVICEID map to an Epic account.
			default:                                return AT::EOS_EAT_EPIC;
			}
		}
	}

	ConnectInterface::ConnectInterface(Platform& Owner) : InterfaceBase(Owner) {}

	EOS_ProductUserId ConnectInterface::LocalUser() const
	{
		return Platform_.LocalIdentity().ProductUserId();
	}

	EOS_ProductUserId ConnectInterface::LoggedInUserByIndex(int32_t Index) const
	{
		return (bLoggedIn_ && Index == 0) ? LocalUser() : nullptr;
	}

	EOS_ELoginStatus ConnectInterface::LoginStatus(EOS_ProductUserId User) const
	{
		if (bLoggedIn_ && User == LocalUser())
		{
			return EOS_ELoginStatus::EOS_LS_LoggedIn;
		}
		return EOS_ELoginStatus::EOS_LS_NotLoggedIn;
	}

	void ConnectInterface::Login(const EOS_Connect_LoginOptions* Options, void* ClientData, EOS_Connect_OnLoginCallback Cb)
	{
		// Capture the identity provider the game authenticated with so the
		// ExternalAccountInfo family reports the real provider (e.g. Steam) rather
		// than a hardcoded Epic account. Credentials is a v1 field (always present
		// when non-null); UserLoginInfo::DisplayName arrives at LoginOptions v2.
		if (Options != nullptr)
		{
			if (Options->Credentials != nullptr)
			{
				ExternalType_ = AccountTypeFromCredential(Options->Credentials->Type);
				// A Steam session ticket carries the real SteamID64; recover it so
				// the ExternalAccountInfo family reports the id Steam itself uses.
				if (Options->Credentials->Type == EOS_EExternalCredentialType::EOS_ECT_STEAM_SESSION_TICKET &&
					Options->Credentials->Token != nullptr)
				{
					Platform_.NoteSteamSessionTicket(Options->Credentials->Token);
				}
			}
			if (HasField(Options->ApiVersion, 2) && Options->UserLoginInfo != nullptr &&
				Options->UserLoginInfo->DisplayName != nullptr)
			{
				ExternalDisplayName_ = Options->UserLoginInfo->DisplayName;
			}
		}
		EOSEMU_LOG(Connect, EOS_ELogLevel::EOS_LOG_Info, "Connect_Login -> shim success");
		const bool WasLoggedIn = bLoggedIn_;
		bLoggedIn_ = true;
		EOS_ProductUserId User = LocalUser();

		if (Cb == nullptr)
		{
			return;
		}
		Platform_.Dispatch().Post([this, Cb, ClientData, User, WasLoggedIn]
		{
			EOS_Connect_LoginCallbackInfo Info = {};
			Info.ResultCode = EOS_EResult::EOS_Success;
			Info.ClientData = ClientData;
			Info.LocalUserId = User;
			Cb(&Info);
			if (!WasLoggedIn)
			{
				for (const auto& E : StatusChanged_.Snapshot())
				{
					EOS_Connect_LoginStatusChangedCallbackInfo N = {};
					N.ClientData = E.ClientData;
					N.LocalUserId = User;
					N.PreviousStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
					N.CurrentStatus = EOS_ELoginStatus::EOS_LS_LoggedIn;
					E.Fn(&N);
				}
			}
		});
	}

	void ConnectInterface::CreateUser(const EOS_Connect_CreateUserOptions* Options, void* ClientData, EOS_Connect_OnCreateUserCallback Cb)
	{
		(void)Options;
		bLoggedIn_ = true;
		EOS_ProductUserId User = LocalUser();
		if (Cb == nullptr)
		{
			return;
		}
		Platform_.Dispatch().Post([Cb, ClientData, User]
		{
			EOS_Connect_CreateUserCallbackInfo Info = {};
			Info.ResultCode = EOS_EResult::EOS_Success;
			Info.ClientData = ClientData;
			Info.LocalUserId = User;
			Cb(&Info);
		});
	}
}

// ----------------------------------------------------------------------------
// Exported entry points.
// ----------------------------------------------------------------------------

using namespace EOSEmu;

namespace
{
	// Posts a simple ResultCode+ClientData+LocalUserId completion for the many
	// Connect async calls that only report success.
	template <typename Info, typename Cb>
	void PostSimple(ConnectInterface* I, Cb Callback, void* ClientData, EOS_ProductUserId User)
	{
		if (I == nullptr || Callback == nullptr)
		{
			return;
		}
		I->Owner().Dispatch().Post([Callback, ClientData, User]
		{
			Info Data = {};
			Data.ResultCode = EOS_EResult::EOS_Success;
			Data.ClientData = ClientData;
			Data.LocalUserId = User;
			Callback(&Data);
		});
	}

	// The external account id string for a given provider. For the local user's
	// Steam account we report the real SteamID64 when one is known (config pin,
	// the loaded steam_api module, or the login ticket -- Platform::LocalSteamId)
	// so it agrees with what the game's own Steam layer sees. Remote peers and
	// the no-Steam case get a stable synthesised id in the individual-account
	// range, derived from the product id so it looks right and stays consistent
	// across a run. Other providers just echo the product id string.
	std::string ExternalAccountId(ConnectInterface* I, EOS_ProductUserId Target, EOS_EExternalAccountType Type)
	{
		const std::string& Product = Ids::ProductString(Target);
		if (Type == EOS_EExternalAccountType::EOS_EAT_STEAM)
		{
			if (I != nullptr && Target == I->Owner().LocalIdentity().ProductUserId())
			{
				const uint64_t Real = I->Owner().LocalSteamId();
				if (Real != 0)
				{
					return std::to_string(Real);
				}
				// Once per process, like stub hits: the game is being told a
				// Steam id its own Steam layer will not recognise.
				static std::atomic<bool> Warned{false};
				if (!Warned.exchange(true))
				{
					EOSEMU_WARN(Connect,
						"no real SteamID64 for the local user (no steam_api module, no session ticket); "
						"reporting a synthesised id -- pin one with [Identity] SteamId");
				}
			}
			const uint64_t Base = 76561197960265728ull; // SteamID64 individual base
			const uint64_t Account = std::hash<std::string>{}(Product) % 2000000000ull;
			return std::to_string(Base + Account);
		}
		return Product;
	}

	EOS_Connect_ExternalAccountInfo* MakeExternalAccountInfo(
		ConnectInterface* I, EOS_ProductUserId Target, const std::string& DisplayName, EOS_EExternalAccountType Type)
	{
		EOS_Connect_ExternalAccountInfo* Info = AllocApi<EOS_Connect_ExternalAccountInfo>();
		Info->ApiVersion = EOS_CONNECT_EXTERNALACCOUNTINFO_API_LATEST;
		Info->ProductUserId = Target;
		Info->DisplayName = AttachString(Info, DisplayName.c_str());
		Info->AccountId = AttachString(Info, ExternalAccountId(I, Target, Type).c_str());
		Info->AccountIdType = Type;
		Info->LastLoginTime = EOS_CONNECT_TIME_UNDEFINED;
		return Info;
	}

	// The provider type and display name to report for a copy, taken from the
	// login the local user performed (falling back to the local identity name).
	EOS_EExternalAccountType ReportedAccountType(ConnectInterface* I)
	{
		return I != nullptr ? I->ExternalAccountType() : EOS_EExternalAccountType::EOS_EAT_EPIC;
	}

	std::string ReportedDisplayName(ConnectInterface* I)
	{
		if (I == nullptr) return {};
		const std::string& FromLogin = I->ExternalDisplayName();
		return FromLogin.empty() ? I->Owner().LocalIdentity().DisplayName() : FromLogin;
	}
}

EOS_DECLARE_FUNC(void) EOS_Connect_Login(EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<ConnectInterface>(Handle))
	{
		I->Login(Options, ClientData, CompletionDelegate);
	}
}

EOS_DECLARE_FUNC(void) EOS_Connect_Logout(EOS_HConnect Handle, const EOS_Connect_LogoutOptions* Options, void* ClientData, const EOS_Connect_OnLogoutCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : nullptr;
	PostSimple<EOS_Connect_LogoutCallbackInfo>(I, CompletionDelegate, ClientData, User);
}

EOS_DECLARE_FUNC(void) EOS_Connect_CreateUser(EOS_HConnect Handle, const EOS_Connect_CreateUserOptions* Options, void* ClientData, const EOS_Connect_OnCreateUserCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<ConnectInterface>(Handle))
	{
		I->CreateUser(Options, ClientData, CompletionDelegate);
	}
}

EOS_DECLARE_FUNC(void) EOS_Connect_LinkAccount(EOS_HConnect Handle, const EOS_Connect_LinkAccountOptions* Options, void* ClientData, const EOS_Connect_OnLinkAccountCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : (I ? I->LocalUser() : nullptr);
	PostSimple<EOS_Connect_LinkAccountCallbackInfo>(I, CompletionDelegate, ClientData, User);
}

EOS_DECLARE_FUNC(void) EOS_Connect_UnlinkAccount(EOS_HConnect Handle, const EOS_Connect_UnlinkAccountOptions* Options, void* ClientData, const EOS_Connect_OnUnlinkAccountCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : (I ? I->LocalUser() : nullptr);
	PostSimple<EOS_Connect_UnlinkAccountCallbackInfo>(I, CompletionDelegate, ClientData, User);
}

EOS_DECLARE_FUNC(void) EOS_Connect_CreateDeviceId(EOS_HConnect Handle, const EOS_Connect_CreateDeviceIdOptions* Options, void* ClientData, const EOS_Connect_OnCreateDeviceIdCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<ConnectInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_Connect_CreateDeviceIdCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Connect_DeleteDeviceId(EOS_HConnect Handle, const EOS_Connect_DeleteDeviceIdOptions* Options, void* ClientData, const EOS_Connect_OnDeleteDeviceIdCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<ConnectInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_Connect_DeleteDeviceIdCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Connect_TransferDeviceIdAccount(EOS_HConnect Handle, const EOS_Connect_TransferDeviceIdAccountOptions* Options, void* ClientData, const EOS_Connect_OnTransferDeviceIdAccountCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<ConnectInterface>(Handle);
	EOS_ProductUserId User = I ? I->LocalUser() : nullptr;
	PostSimple<EOS_Connect_TransferDeviceIdAccountCallbackInfo>(I, CompletionDelegate, ClientData, User);
}

EOS_DECLARE_FUNC(void) EOS_Connect_QueryExternalAccountMappings(EOS_HConnect Handle, const EOS_Connect_QueryExternalAccountMappingsOptions* Options, void* ClientData, const EOS_Connect_OnQueryExternalAccountMappingsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : (I ? I->LocalUser() : nullptr);
	PostSimple<EOS_Connect_QueryExternalAccountMappingsCallbackInfo>(I, CompletionDelegate, ClientData, User);
}

EOS_DECLARE_FUNC(void) EOS_Connect_QueryProductUserIdMappings(EOS_HConnect Handle, const EOS_Connect_QueryProductUserIdMappingsOptions* Options, void* ClientData, const EOS_Connect_OnQueryProductUserIdMappingsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	EOS_ProductUserId User = (Options != nullptr) ? Options->LocalUserId : (I ? I->LocalUser() : nullptr);
	PostSimple<EOS_Connect_QueryProductUserIdMappingsCallbackInfo>(I, CompletionDelegate, ClientData, User);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetExternalAccountMapping(EOS_HConnect Handle, const EOS_Connect_GetExternalAccountMappingsOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || Options->TargetExternalUserId == nullptr)
	{
		return nullptr;
	}
	// External ID string -> interned Product User ID. In the LAN model the
	// external id and the product id share a namespace.
	return Ids::InternProduct(Options->TargetExternalUserId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_GetProductUserIdMapping(EOS_HConnect Handle, const EOS_Connect_GetProductUserIdMappingOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || InOutBufferLength == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	// The external "mapping" for a product user is its own id string.
	const std::string& Value = Ids::ProductString(Options->TargetProductUserId);
	if (Value.empty())
	{
		return EOS_EResult::EOS_NotFound;
	}
	const int32_t Required = static_cast<int32_t>(Value.size()) + 1;
	if (OutBuffer == nullptr || *InOutBufferLength < Required)
	{
		*InOutBufferLength = Required;
		return EOS_EResult::EOS_LimitExceeded;
	}
	std::memcpy(OutBuffer, Value.c_str(), Value.size() + 1);
	*InOutBufferLength = Required;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Connect_GetProductUserExternalAccountCount(EOS_HConnect Handle, const EOS_Connect_GetProductUserExternalAccountCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	// Every product user has exactly one (synthetic Epic) external account.
	return (Options != nullptr && Options->TargetUserId != nullptr) ? 1u : 0u;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByIndex(EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByIndexOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	if (OutExternalAccountInfo == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutExternalAccountInfo = nullptr;
	if (Options->TargetUserId == nullptr || Options->ExternalAccountInfoIndex != 0) return EOS_EResult::EOS_NotFound;
	*OutExternalAccountInfo = MakeExternalAccountInfo(I, Options->TargetUserId, ReportedDisplayName(I), ReportedAccountType(I));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByAccountType(EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	if (OutExternalAccountInfo == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutExternalAccountInfo = nullptr;
	// We hold exactly one external account for the local user: the provider it
	// logged in with. A query for any other provider legitimately misses.
	if (Options->TargetUserId == nullptr || Options->AccountIdType != ReportedAccountType(I)) return EOS_EResult::EOS_NotFound;
	*OutExternalAccountInfo = MakeExternalAccountInfo(I, Options->TargetUserId, ReportedDisplayName(I), ReportedAccountType(I));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserExternalAccountByAccountId(EOS_HConnect Handle, const EOS_Connect_CopyProductUserExternalAccountByAccountIdOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	if (OutExternalAccountInfo == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutExternalAccountInfo = nullptr;
	if (Options->TargetUserId == nullptr) return EOS_EResult::EOS_NotFound;
	*OutExternalAccountInfo = MakeExternalAccountInfo(I, Options->TargetUserId, ReportedDisplayName(I), ReportedAccountType(I));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyProductUserInfo(EOS_HConnect Handle, const EOS_Connect_CopyProductUserInfoOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	if (OutExternalAccountInfo == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutExternalAccountInfo = nullptr;
	if (Options->TargetUserId == nullptr) return EOS_EResult::EOS_NotFound;
	*OutExternalAccountInfo = MakeExternalAccountInfo(I, Options->TargetUserId, ReportedDisplayName(I), ReportedAccountType(I));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(int32_t) EOS_Connect_GetLoggedInUsersCount(EOS_HConnect Handle)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	return I ? I->LoggedInUsersCount() : 0;
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetLoggedInUserByIndex(EOS_HConnect Handle, int32_t Index)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	return I ? I->LoggedInUserByIndex(Index) : nullptr;
}

EOS_DECLARE_FUNC(EOS_ELoginStatus) EOS_Connect_GetLoginStatus(EOS_HConnect Handle, EOS_ProductUserId LocalUserId)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	return I ? I->LoginStatus(LocalUserId) : EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyAuthExpiration(EOS_HConnect Handle, const EOS_Connect_AddNotifyAuthExpirationOptions* Options, void* ClientData, const EOS_Connect_OnAuthExpirationCallback Notification)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<ConnectInterface>(Handle);
	return I ? I->AddNotifyAuthExpiration(Notification, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Connect_RemoveNotifyAuthExpiration(EOS_HConnect Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<ConnectInterface>(Handle)) I->RemoveNotifyAuthExpiration(InId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyLoginStatusChanged(EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback Notification)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<ConnectInterface>(Handle);
	return I ? I->AddNotifyLoginStatusChanged(Notification, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_Connect_RemoveNotifyLoginStatusChanged(EOS_HConnect Handle, EOS_NotificationId InId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<ConnectInterface>(Handle)) I->RemoveNotifyLoginStatusChanged(InId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyIdToken(EOS_HConnect Handle, const EOS_Connect_CopyIdTokenOptions* Options, EOS_Connect_IdToken** OutIdToken)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	if (OutIdToken == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutIdToken = nullptr;
	if (I == nullptr || !I->IsLoggedIn()) return EOS_EResult::EOS_NotFound;
	EOS_ProductUserId User = (Options != nullptr && Options->LocalUserId != nullptr) ? Options->LocalUserId : I->LocalUser();

	EOS_Connect_IdToken* Token = AllocApi<EOS_Connect_IdToken>();
	Token->ApiVersion = EOS_CONNECT_IDTOKEN_API_LATEST;
	Token->ProductUserId = User;
	Token->JsonWebToken = AttachString(Token, "eyJhbGciOiJub25lIn0.eyJpc3MiOiJlb3NlbXUifQ.");
	*OutIdToken = Token;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Connect_VerifyIdToken(EOS_HConnect Handle, const EOS_Connect_VerifyIdTokenOptions* Options, void* ClientData, const EOS_Connect_OnVerifyIdTokenCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<ConnectInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_ProductUserId User = (Options != nullptr && Options->IdToken != nullptr) ? Options->IdToken->ProductUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, User]
	{
		EOS_Connect_VerifyIdTokenCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.ProductUserId = User;
		Info.bIsAccountInfoPresent = EOS_FALSE;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Connect_ExternalAccountInfo_Release(EOS_Connect_ExternalAccountInfo* ExternalAccountInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(ExternalAccountInfo);
}

EOS_DECLARE_FUNC(void) EOS_Connect_IdToken_Release(EOS_Connect_IdToken* IdToken)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(IdToken);
}
