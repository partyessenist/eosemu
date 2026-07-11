//
// Opaque ID interning and the eos_common.h ID entry points.
//
// The *Details structs the SDK typedefs to pointers are completed here. They
// are allocated once per distinct string and deliberately never freed -- the
// contract is that the pointer stays valid for the process lifetime.
//

#include "core/Ids.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

#include "eos_common.h"

// Complete the opaque handle types. One instance is interned per ID string.
struct EOS_EpicAccountIdDetails
{
	std::string Value;
};

struct EOS_ProductUserIdDetails
{
	std::string Value;
};

struct EOS_ContinuanceTokenDetails
{
	std::string Value;
};

namespace EOSEmu
{
	namespace Ids
	{
		namespace
		{
			std::mutex g_Mutex;
			std::unordered_map<std::string, EOS_EpicAccountIdDetails*> g_Epic;
			std::unordered_map<std::string, EOS_ProductUserIdDetails*> g_Product;
			std::unordered_map<std::string, EOS_ContinuanceTokenDetails*> g_Continuance;

			const std::string g_Empty;

			template <typename Details, typename Map>
			Details* Intern(Map& Table, const std::string& Value)
			{
				if (Value.empty())
				{
					return nullptr;
				}
				std::lock_guard<std::mutex> Lock(g_Mutex);
				auto It = Table.find(Value);
				if (It != Table.end())
				{
					return It->second;
				}
				// Intentionally never freed: the pointer must outlive every
				// consumer copy of it.
				Details* Fresh = new Details{Value};
				Table.emplace(Value, Fresh);
				return Fresh;
			}
		}

		EOS_EpicAccountId InternEpic(const std::string& Value)
		{
			return Intern<EOS_EpicAccountIdDetails>(g_Epic, Value);
		}

		EOS_ProductUserId InternProduct(const std::string& Value)
		{
			return Intern<EOS_ProductUserIdDetails>(g_Product, Value);
		}

		EOS_ContinuanceToken InternContinuance(const std::string& Value)
		{
			return Intern<EOS_ContinuanceTokenDetails>(g_Continuance, Value);
		}

		const std::string& EpicString(EOS_EpicAccountId Id)
		{
			return Id != nullptr ? Id->Value : g_Empty;
		}

		const std::string& ProductString(EOS_ProductUserId Id)
		{
			return Id != nullptr ? Id->Value : g_Empty;
		}
	}
}

namespace
{
	// Shared body for the three *_ToString entry points, which are identical but
	// for the value they stringify and use int32_t lengths. Oracle-verified
	// ordering: a null length is InvalidParameters, an invalid ID is InvalidUser
	// (length untouched), and a null buffer is a size query -- LimitExceeded with
	// the required length written back, same as an undersized buffer.
	EOS_EResult CopyIdString(const std::string& Value, bool bValid, char* OutBuffer, int32_t* InOutBufferLength)
	{
		if (InOutBufferLength == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		if (!bValid)
		{
			return EOS_EResult::EOS_InvalidUser;
		}

		// Required buffer size includes the null terminator.
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
}

// ----------------------------------------------------------------------------
// Epic Account ID
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_Bool) EOS_EpicAccountId_IsValid(EOS_EpicAccountId AccountId)
{
	// The header notes FromString always yields a "valid" ID, so validity here
	// is simply "interned and non-empty".
	return (AccountId != nullptr && !AccountId->Value.empty()) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_EpicAccountId_ToString(EOS_EpicAccountId AccountId, char* OutBuffer, int32_t* InOutBufferLength)
{
	const std::string& Value = EOSEmu::Ids::EpicString(AccountId);
	return CopyIdString(Value, AccountId != nullptr, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_EpicAccountId_FromString(const char* AccountIdString)
{
	if (AccountIdString == nullptr)
	{
		return nullptr;
	}
	return EOSEmu::Ids::InternEpic(AccountIdString);
}

// ----------------------------------------------------------------------------
// Product User ID
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_Bool) EOS_ProductUserId_IsValid(EOS_ProductUserId AccountId)
{
	return (AccountId != nullptr && !AccountId->Value.empty()) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_ProductUserId_ToString(EOS_ProductUserId AccountId, char* OutBuffer, int32_t* InOutBufferLength)
{
	const std::string& Value = EOSEmu::Ids::ProductString(AccountId);
	return CopyIdString(Value, AccountId != nullptr, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_ProductUserId_FromString(const char* ProductUserIdString)
{
	if (ProductUserIdString == nullptr)
	{
		return nullptr;
	}
	return EOSEmu::Ids::InternProduct(ProductUserIdString);
}

// ----------------------------------------------------------------------------
// Continuance token
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_ContinuanceToken_ToString(EOS_ContinuanceToken ContinuanceToken, char* OutBuffer, int32_t* InOutBufferLength)
{
	// Unlike the account-ID ToStrings, a null token is InvalidParameters, not
	// InvalidUser (oracle-verified).
	if (ContinuanceToken == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	return CopyIdString(ContinuanceToken->Value, true, OutBuffer, InOutBufferLength);
}

// ----------------------------------------------------------------------------
// Byte array to hex string
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_ByteArray_ToString(const uint8_t* ByteArray, const uint32_t Length, char* OutBuffer, uint32_t* InOutBufferLength)
{
	if (ByteArray == nullptr || InOutBufferLength == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}

	// Two hex chars per byte plus a null terminator.
	const uint32_t Required = Length * 2u + 1u;
	if (OutBuffer == nullptr || *InOutBufferLength < Required)
	{
		*InOutBufferLength = Required;
		return EOS_EResult::EOS_LimitExceeded;
	}

	static const char kHex[] = "0123456789ABCDEF";
	for (uint32_t i = 0; i < Length; ++i)
	{
		OutBuffer[i * 2] = kHex[(ByteArray[i] >> 4) & 0xF];
		OutBuffer[i * 2 + 1] = kHex[ByteArray[i] & 0xF];
	}
	OutBuffer[Length * 2] = '\0';
	*InOutBufferLength = Required;
	return EOS_EResult::EOS_Success;
}
