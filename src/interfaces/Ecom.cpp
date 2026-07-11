//
// Ecom interface -- entitlements and ownership from config; storefront inert.
//
// The entitlement/ownership entry points answer from the [Ecom] block of
// eosemu.ini (parsed lazily on first use). Everything to do with a catalog
// backend -- offers, items, images, releases, checkout, transactions -- stays a
// generated stub (count 0), because there is no store to model. See Ecom.h and
// HANDOFF.md Task 1c.
//

#include "interfaces/Ecom.h"

#include "core/Config.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_ecom.h"

#include <cstring>

namespace EOSEmu
{
	namespace
	{
		// Splits "name:catalogItemId[:entitlementId]" into its (up to) three parts.
		std::vector<std::string> SplitColon(const std::string& S)
		{
			std::vector<std::string> Parts;
			size_t Start = 0;
			while (true)
			{
				const size_t Colon = S.find(':', Start);
				if (Colon == std::string::npos)
				{
					Parts.push_back(S.substr(Start));
					break;
				}
				Parts.push_back(S.substr(Start, Colon - Start));
				Start = Colon + 1;
			}
			return Parts;
		}
	}

	void EcomInterface::EnsureLoaded()
	{
		// Caller holds Mutex_.
		if (Loaded_) return;
		Loaded_ = true;

		Config& Cfg = Platform_.Config();
		for (const std::string& Raw : Cfg.GetList("Ecom", "Entitlement"))
		{
			const std::vector<std::string> Parts = SplitColon(Raw);
			if (Parts.empty() || Parts[0].empty()) continue;
			Entitlement E;
			E.Name = Parts[0];
			E.CatalogItemId = (Parts.size() > 1 && !Parts[1].empty()) ? Parts[1] : Parts[0];
			E.EntitlementId = (Parts.size() > 2 && !Parts[2].empty()) ? Parts[2] : Parts[0];
			Entitlements_.push_back(std::move(E));
		}
		for (const std::string& Item : Cfg.GetList("Ecom", "OwnedItem"))
		{
			if (!Item.empty()) OwnedItems_.push_back(Item);
		}

		if (!Entitlements_.empty() || !OwnedItems_.empty())
		{
			EOSEMU_INFO(Ecom, "Ecom config: %zu entitlement(s), %zu owned item(s)",
				Entitlements_.size(), OwnedItems_.size());
		}
	}

	bool EcomInterface::IsOwned(const std::string& CatalogItemId)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		for (const std::string& Item : OwnedItems_)
			if (Item == CatalogItemId) return true;
		for (const Entitlement& E : Entitlements_)
			if (E.CatalogItemId == CatalogItemId) return true;
		return false;
	}

	std::vector<EcomInterface::Entitlement> EcomInterface::Entitlements()
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		return Entitlements_;
	}

	std::vector<std::string> EcomInterface::OwnedItems()
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		// The union of explicit owned items and entitlement-granted items.
		std::vector<std::string> Out = OwnedItems_;
		for (const Entitlement& E : Entitlements_)
		{
			bool Present = false;
			for (const std::string& I : Out) if (I == E.CatalogItemId) { Present = true; break; }
			if (!Present) Out.push_back(E.CatalogItemId);
		}
		return Out;
	}

	uint32_t EcomInterface::Redeem(const std::vector<std::string>& EntitlementIds)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		EnsureLoaded();
		LastRedeemed_.clear();
		uint32_t Count = 0;
		for (const std::string& Id : EntitlementIds)
		{
			for (Entitlement& E : Entitlements_)
			{
				if (E.EntitlementId == Id)
				{
					E.Redeemed = true;
					LastRedeemed_.push_back(E.EntitlementId);
					++Count;
				}
			}
		}
		return Count;
	}

	std::vector<std::string> EcomInterface::LastRedeemed()
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		return LastRedeemed_;
	}

	namespace
	{
		EOS_Ecom_Entitlement* MakeEntitlement(const EcomInterface::Entitlement& E)
		{
			EOS_Ecom_Entitlement* Out = AllocApi<EOS_Ecom_Entitlement>();
			Out->ApiVersion = EOS_ECOM_ENTITLEMENT_API_LATEST;
			Out->EntitlementName = AttachString(Out, E.Name.c_str());
			Out->EntitlementId = AttachString(Out, E.EntitlementId.c_str());
			Out->CatalogItemId = AttachString(Out, E.CatalogItemId.c_str());
			Out->ServerIndex = -1;
			Out->bRedeemed = E.Redeemed ? EOS_TRUE : EOS_FALSE;
			Out->EndTimestamp = EOS_ECOM_ENTITLEMENT_ENDTIMESTAMP_UNDEFINED;
			return Out;
		}
	}
}

using namespace EOSEmu;

// ----------------------------------------------------------------------------
// Ownership queries.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOwnership(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	std::vector<std::string> Ids;
	std::vector<EOS_EOwnershipStatus> Status;
	if (Options != nullptr)
	{
		for (uint32_t i = 0; Options->CatalogItemIds != nullptr && i < Options->CatalogItemIdCount; ++i)
		{
			const char* Id = Options->CatalogItemIds[i];
			if (Id == nullptr) continue;
			Ids.push_back(Id);
			Status.push_back(I->IsOwned(Id) ? EOS_EOwnershipStatus::EOS_OS_Owned : EOS_EOwnershipStatus::EOS_OS_NotOwned);
		}
	}

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Ids, Status]
	{
		// Build the borrowed ItemOwnership array here, where Ids is a stable
		// captured member for the duration of the callback.
		std::vector<EOS_Ecom_ItemOwnership> Items(Ids.size());
		for (size_t i = 0; i < Ids.size(); ++i)
		{
			Items[i].ApiVersion = EOS_ECOM_ITEMOWNERSHIP_API_LATEST;
			Items[i].Id = Ids[i].c_str();
			Items[i].OwnershipStatus = Status[i];
		}
		EOS_Ecom_QueryOwnershipCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.ItemOwnership = Items.empty() ? nullptr : Items.data();
		Info.ItemOwnershipCount = static_cast<uint32_t>(Items.size());
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOwnershipBySandboxIds(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipBySandboxIdsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;

	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	std::vector<std::string> Sandboxes;
	if (Options != nullptr)
	{
		for (uint32_t i = 0; Options->SandboxIds != nullptr && i < Options->SandboxIdsCount; ++i)
		{
			if (Options->SandboxIds[i] != nullptr) Sandboxes.push_back(Options->SandboxIds[i]);
		}
	}
	std::vector<std::string> Owned = I->OwnedItems();

	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Sandboxes, Owned]
	{
		// Stable backing for the borrowed const char* arrays: one item-pointer
		// vector per sandbox, all rooted in the captured Owned/Sandboxes.
		std::vector<const char*> OwnedPtrs;
		OwnedPtrs.reserve(Owned.size());
		for (const std::string& Item : Owned) OwnedPtrs.push_back(Item.c_str());

		std::vector<EOS_Ecom_SandboxIdItemOwnership> Results(Sandboxes.size());
		for (size_t i = 0; i < Sandboxes.size(); ++i)
		{
			Results[i].SandboxId = Sandboxes[i].c_str();
			Results[i].OwnedCatalogItemIds = OwnedPtrs.empty() ? nullptr : OwnedPtrs.data();
			Results[i].OwnedCatalogItemIdsCount = static_cast<uint32_t>(OwnedPtrs.size());
		}
		EOS_Ecom_QueryOwnershipBySandboxIdsCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.SandboxIdItemOwnerships = Results.empty() ? nullptr : Results.data();
		Info.SandboxIdItemOwnershipsCount = static_cast<uint32_t>(Results.size());
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOwnershipToken(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipTokenCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local]
	{
		// The token is a signed JWT an external server verifies; with no backend
		// signing key we return a fixed non-empty placeholder.
		const char* Token = "eosemu-ownership-token";
		EOS_Ecom_QueryOwnershipTokenCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.OwnershipToken = Token;
		CompletionDelegate(&Info);
	});
}

// ----------------------------------------------------------------------------
// Entitlement queries.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_Ecom_QueryEntitlements(EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local]
	{
		// Entitlements are served from config; the query itself just succeeds so
		// the subsequent Get*/Copy* getters return data.
		EOS_Ecom_QueryEntitlementsCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_Ecom_QueryEntitlementToken(EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementTokenCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local]
	{
		const char* Token = "eosemu-entitlement-token";
		EOS_Ecom_QueryEntitlementTokenCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.EntitlementToken = Token;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetEntitlementsCount(EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<EcomInterface>(Handle);
	return I ? static_cast<uint32_t>(I->Entitlements().size()) : 0;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetEntitlementsByNameCount(EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsByNameCountOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || Options == nullptr || Options->EntitlementName == nullptr) return 0;
	uint32_t Count = 0;
	for (const auto& E : I->Entitlements())
		if (E.Name == Options->EntitlementName) ++Count;
	return Count;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyEntitlementByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutEntitlement == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutEntitlement = nullptr;
	const auto All = I->Entitlements();
	if (Options->EntitlementIndex >= All.size()) return EOS_EResult::EOS_NotFound;
	*OutEntitlement = MakeEntitlement(All[Options->EntitlementIndex]);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyEntitlementByNameAndIndex(EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByNameAndIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutEntitlement == nullptr || Options->EntitlementName == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutEntitlement = nullptr;
	uint32_t Seen = 0;
	for (const auto& E : I->Entitlements())
	{
		if (E.Name != Options->EntitlementName) continue;
		if (Seen == Options->Index)
		{
			*OutEntitlement = MakeEntitlement(E);
			return EOS_EResult::EOS_Success;
		}
		++Seen;
	}
	return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyEntitlementById(EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIdOptions* Options, EOS_Ecom_Entitlement** OutEntitlement)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutEntitlement == nullptr || Options->EntitlementId == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutEntitlement = nullptr;
	for (const auto& E : I->Entitlements())
	{
		if (E.EntitlementId == Options->EntitlementId)
		{
			*OutEntitlement = MakeEntitlement(E);
			return EOS_EResult::EOS_Success;
		}
	}
	return EOS_EResult::EOS_NotFound;
}

// ----------------------------------------------------------------------------
// Redemption.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_Ecom_RedeemEntitlements(EOS_HEcom Handle, const EOS_Ecom_RedeemEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnRedeemEntitlementsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || CompletionDelegate == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	std::vector<std::string> Ids;
	if (Options != nullptr)
	{
		for (uint32_t i = 0; Options->EntitlementIds != nullptr && i < Options->EntitlementIdCount; ++i)
			if (Options->EntitlementIds[i] != nullptr) Ids.push_back(Options->EntitlementIds[i]);
	}
	const uint32_t Redeemed = I->Redeem(Ids);
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Redeemed]
	{
		EOS_Ecom_RedeemEntitlementsCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.RedeemedEntitlementIdsCount = Redeemed;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetLastRedeemedEntitlementsCount(EOS_HEcom Handle, const EOS_Ecom_GetLastRedeemedEntitlementsCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<EcomInterface>(Handle);
	return I ? static_cast<uint32_t>(I->LastRedeemed().size()) : 0;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyLastRedeemedEntitlementByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyLastRedeemedEntitlementByIndexOptions* Options, char* OutRedeemedEntitlementId, int32_t* InOutRedeemedEntitlementIdLength)
{
	EOSEMU_API_TRACE();
	auto* I = As<EcomInterface>(Handle);
	if (I == nullptr || Options == nullptr || OutRedeemedEntitlementId == nullptr || InOutRedeemedEntitlementIdLength == nullptr)
		return EOS_EResult::EOS_InvalidParameters;
	const auto Ids = I->LastRedeemed();
	if (Options->RedeemedEntitlementIndex >= Ids.size()) return EOS_EResult::EOS_NotFound;
	const std::string& Id = Ids[Options->RedeemedEntitlementIndex];
	const int32_t Required = static_cast<int32_t>(Id.size()) + 1;
	if (*InOutRedeemedEntitlementIdLength < Required)
	{
		*InOutRedeemedEntitlementIdLength = Required;
		return EOS_EResult::EOS_LimitExceeded;
	}
	std::memcpy(OutRedeemedEntitlementId, Id.c_str(), Id.size() + 1);
	*InOutRedeemedEntitlementIdLength = Required;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Ecom_Entitlement_Release(EOS_Ecom_Entitlement* Entitlement)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(Entitlement);
}
