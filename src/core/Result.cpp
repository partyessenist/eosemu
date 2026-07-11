//
// EOS_EResult helpers.
//
// These are pure functions with no SDK state, and Epic's shipped DLL fully
// specifies them. Behaviour below was read off SDK/Bin/EOSSDK-Win64-Shipping.dll
// by calling it for all 234 result codes -- not guessed.
//
// They are implemented rather than stubbed because a stub would be actively
// harmful:
//   * EOS_EResult_ToString is documented "The return value is never null"
//     (eos_common.h:25). Returning nullptr crashes the first error-logging path.
//   * EOS_EResult_IsOperationComplete returning false tells the caller its
//     callback will fire again, so the caller waits forever.
//

#include "eos_common.h"

// eos_result.h has no include guard and is designed to be re-included with
// EOS_RESULT_VALUE redefined. eos_common.h leaves both macros undefined.
EOS_DECLARE_FUNC(const char*) EOS_EResult_ToString(EOS_EResult Result)
{
	switch (Result)
	{
#define EOS_RESULT_VALUE(Name, Value) case EOS_EResult::Name: return #Name;
#define EOS_RESULT_VALUE_LAST(Name, Value) case EOS_EResult::Name: return #Name;
#include "eos_result.h"
#undef EOS_RESULT_VALUE
#undef EOS_RESULT_VALUE_LAST

	default:
		// Matches the shipping SDK, which returns this for any unmapped value.
		return "UNKNOWN_RESULT";
	}
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_EResult_IsOperationComplete(EOS_EResult Result)
{
	// Exactly three codes mean "this callback will be invoked again". Verified
	// against the shipping SDK across every declared result code; every other
	// value, including values outside the enum, reports complete.
	switch (Result)
	{
	case EOS_EResult::EOS_OperationWillRetry:  // 19
	case EOS_EResult::EOS_Auth_PinGrantCode:   // 1020 -- awaiting user PIN entry
	case EOS_EResult::EOS_Auth_MFARequired:    // 1060 -- awaiting second factor
		return EOS_FALSE;

	default:
		return EOS_TRUE;
	}
}
