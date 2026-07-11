//
// UserInfo interface -- display names resolved from the LAN peer directory.
//

#include "interfaces/UserInfo.h"
#include "core/Logging.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Memory.h"
#include "core/Peers.h"
#include "core/Platform.h"

namespace EOSEmu
{
	std::string UserInfoInterface::DisplayNameFor(EOS_EpicAccountId Account)
	{
		if (Account == nullptr) return {};
		if (Account == Platform_.LocalIdentity().EpicAccountId())
			return Platform_.LocalIdentity().DisplayName();
		PeerInfo Info;
		if (Platform_.Peers().FindByEpic(Account, Info)) return Info.DisplayName;
		return {};
	}

	void UserInfoInterface::QueryUserInfo(EOS_EpicAccountId Local, EOS_EpicAccountId Target, void* ClientData, EOS_UserInfo_OnQueryUserInfoCallback Cb)
	{
		if (Cb == nullptr) return;
		Platform_.Dispatch().Post([Cb, ClientData, Local, Target]
		{
			EOS_UserInfo_QueryUserInfoCallbackInfo Info = {};
			Info.ResultCode = EOS_EResult::EOS_Success;
			Info.ClientData = ClientData;
			Info.LocalUserId = Local;
			Info.TargetUserId = Target;
			Cb(&Info);
		});
	}

	EOS_EResult UserInfoInterface::CopyUserInfo(EOS_EpicAccountId Target, EOS_UserInfo** Out)
	{
		if (Out == nullptr || Target == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		const std::string Name = DisplayNameFor(Target);
		if (Name.empty()) return EOS_EResult::EOS_NotFound;

		EOS_UserInfo* Info = AllocApi<EOS_UserInfo>();
		Info->ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
		Info->UserId = Target;
		Info->Country = nullptr;
		Info->DisplayName = AttachString(Info, Name.c_str());
		Info->PreferredLanguage = AttachString(Info, "en");
		Info->Nickname = nullptr;
		Info->DisplayNameSanitized = AttachString(Info, Name.c_str());
		*Out = Info;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult UserInfoInterface::CopyBestDisplayName(EOS_EpicAccountId Target, EOS_UserInfo_BestDisplayName** Out)
	{
		if (Out == nullptr || Target == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*Out = nullptr;
		const std::string Name = DisplayNameFor(Target);
		if (Name.empty()) return EOS_EResult::EOS_NotFound;

		EOS_UserInfo_BestDisplayName* Info = AllocApi<EOS_UserInfo_BestDisplayName>();
		Info->ApiVersion = EOS_USERINFO_BESTDISPLAYNAME_API_LATEST;
		Info->UserId = Target;
		Info->DisplayName = AttachString(Info, Name.c_str());
		Info->DisplayNameSanitized = AttachString(Info, Name.c_str());
		Info->Nickname = nullptr;
		Info->PlatformType = EOS_OPT_Epic;
		*Out = Info;
		return EOS_EResult::EOS_Success;
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfo(EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UserInfoInterface>(Handle);
	if (I == nullptr) return;
	I->QueryUserInfo(Options ? Options->LocalUserId : nullptr, Options ? Options->TargetUserId : nullptr, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfoByDisplayName(EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoByDisplayNameOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UserInfoInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	const char* Name = Options ? Options->DisplayName : nullptr;
	std::string NameCopy = Name ? Name : "";
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, NameCopy]
	{
		EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_NotFound; // display-name lookup unsupported on LAN
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = nullptr;
		Info.DisplayName = NameCopy.c_str();
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfoByExternalAccount(EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoByExternalAccountOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UserInfoInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData]
	{
		EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_NotFound;
		Info.ClientData = ClientData;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyUserInfo(EOS_HUserInfo Handle, const EOS_UserInfo_CopyUserInfoOptions* Options, EOS_UserInfo** OutUserInfo)
{
	EOSEMU_API_TRACE();
	auto* I = As<UserInfoInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopyUserInfo(Options->TargetUserId, OutUserInfo);
}

EOS_DECLARE_FUNC(uint32_t) EOS_UserInfo_GetExternalUserInfoCount(EOS_HUserInfo Handle, const EOS_UserInfo_GetExternalUserInfoCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByIndex(EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByIndexOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutExternalUserInfo) *OutExternalUserInfo = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByAccountType(EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutExternalUserInfo) *OutExternalUserInfo = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByAccountId(EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByAccountIdOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	if (OutExternalUserInfo) *OutExternalUserInfo = nullptr;
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyBestDisplayName(EOS_HUserInfo Handle, const EOS_UserInfo_CopyBestDisplayNameOptions* Options, EOS_UserInfo_BestDisplayName** OutBestDisplayName)
{
	EOSEMU_API_TRACE();
	auto* I = As<UserInfoInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopyBestDisplayName(Options->TargetUserId, OutBestDisplayName);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyBestDisplayNameWithPlatform(EOS_HUserInfo Handle, const EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions* Options, EOS_UserInfo_BestDisplayName** OutBestDisplayName)
{
	EOSEMU_API_TRACE();
	auto* I = As<UserInfoInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->CopyBestDisplayName(Options->TargetUserId, OutBestDisplayName);
}

EOS_DECLARE_FUNC(EOS_OnlinePlatformType) EOS_UserInfo_GetLocalPlatformType(EOS_HUserInfo Handle, const EOS_UserInfo_GetLocalPlatformTypeOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	return EOS_OPT_Epic;
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_Release(EOS_UserInfo* UserInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(UserInfo);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_ExternalUserInfo_Release(EOS_UserInfo_ExternalUserInfo* ExternalUserInfo)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(ExternalUserInfo);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_BestDisplayName_Release(EOS_UserInfo_BestDisplayName* BestDisplayName)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(BestDisplayName);
}
