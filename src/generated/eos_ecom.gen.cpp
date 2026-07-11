// ============================================================================
//  GENERATED FILE -- DO NOT EDIT BY HAND.
//
//  Regenerate with:  python tools/gen_stubs.py
//
//  Every function the EOS SDK declares must be exported or the host process
//  fails to load.  These are placeholders: they trace the call and return an
//  inert value.  To implement one for real, add its name to
//  tools/hand_implemented.txt and define it in a hand-written source file.
// ============================================================================


#include "core/Stub.h"

#include "eos_ecom.h"

EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOffers(EOS_HEcom Handle, const EOS_Ecom_QueryOffersOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOffersCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(void) EOS_Ecom_Checkout(EOS_HEcom Handle, const EOS_Ecom_CheckoutOptions* Options, void* ClientData, const EOS_Ecom_OnCheckoutCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetLastRedeemEntitlementsResultCount(EOS_HEcom Handle, const EOS_Ecom_GetLastRedeemEntitlementsResultCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyLastRedeemEntitlementsResultByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyLastRedeemEntitlementsResultByIndexOptions* Options, char* OutEntitlementId, int32_t* InOutEntitlementIdLength)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetOfferCount(EOS_HEcom Handle, const EOS_Ecom_GetOfferCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyOfferByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyOfferByIndexOptions* Options, EOS_Ecom_CatalogOffer ** OutOffer)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyOfferById(EOS_HEcom Handle, const EOS_Ecom_CopyOfferByIdOptions* Options, EOS_Ecom_CatalogOffer ** OutOffer)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetOfferItemCount(EOS_HEcom Handle, const EOS_Ecom_GetOfferItemCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyOfferItemByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyOfferItemByIndexOptions* Options, EOS_Ecom_CatalogItem ** OutItem)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyItemById(EOS_HEcom Handle, const EOS_Ecom_CopyItemByIdOptions* Options, EOS_Ecom_CatalogItem ** OutItem)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetOfferImageInfoCount(EOS_HEcom Handle, const EOS_Ecom_GetOfferImageInfoCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyOfferImageInfoByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyOfferImageInfoByIndexOptions* Options, EOS_Ecom_KeyImageInfo ** OutImageInfo)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetItemImageInfoCount(EOS_HEcom Handle, const EOS_Ecom_GetItemImageInfoCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyItemImageInfoByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyItemImageInfoByIndexOptions* Options, EOS_Ecom_KeyImageInfo ** OutImageInfo)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetItemReleaseCount(EOS_HEcom Handle, const EOS_Ecom_GetItemReleaseCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyItemReleaseByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyItemReleaseByIndexOptions* Options, EOS_Ecom_CatalogRelease ** OutRelease)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetTransactionCount(EOS_HEcom Handle, const EOS_Ecom_GetTransactionCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyTransactionByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyTransactionByIndexOptions* Options, EOS_Ecom_HTransaction* OutTransaction)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyTransactionById(EOS_HEcom Handle, const EOS_Ecom_CopyTransactionByIdOptions* Options, EOS_Ecom_HTransaction* OutTransaction)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_Transaction_GetTransactionId(EOS_Ecom_HTransaction Handle, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_Transaction_GetEntitlementsCount(EOS_Ecom_HTransaction Handle, const EOS_Ecom_Transaction_GetEntitlementsCountOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return static_cast<uint32_t>(0);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_Transaction_CopyEntitlementByIndex(EOS_Ecom_HTransaction Handle, const EOS_Ecom_Transaction_CopyEntitlementByIndexOptions* Options, EOS_Ecom_Entitlement ** OutEntitlement)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}
