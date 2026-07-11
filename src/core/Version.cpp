#include "eos_version.h"

//
// Epic's shipping 1.19.1.2 DLL reports "1.19.1.2-53289219": EOS_VERSION_STRING
// (MAJOR.MINOR.PATCH.HOTFIX-CHANGELIST) with the changelist their build was cut
// from. We mimic that byte-for-byte, because EOSEmu is a drop-in replacement and
// any consumer that fingerprints the SDK should see what it expects.
//
// The changelist is injected through Epic's own BUILT_FROM_CHANGELIST macro
// (set in CMakeLists.txt), not by hardcoding the string here -- eos_version.h
// already assembles MAJOR.MINOR.PATCH-CHANGELIST from it. Bumping the vendored
// SDK therefore changes the version triple automatically, and the assertion
// below fires so the changelist gets re-checked against the new shipping DLL.
//
// Ground truth: LoadLibrary the real DLL and call EOS_GetVersion.
//

namespace
{
	constexpr bool StringsEqual(const char* A, const char* B)
	{
		return (*A == *B) && (*A == '\0' || StringsEqual(A + 1, B + 1));
	}
}

static_assert(
	StringsEqual(EOS_VERSION_STRING, "1.19.1.2-53289219"),
	"EOS_GetVersion must report exactly what Epic's shipped EOSSDK 1.19.1.2 DLL reports. "
	"If the vendored SDK changed, re-read the real DLL's EOS_GetVersion and update "
	"EOSEMU_EOS_CHANGELIST in CMakeLists.txt along with this assertion.");

EOS_DECLARE_FUNC(const char*) EOS_GetVersion(void)
{
	return EOS_VERSION_STRING;
}
