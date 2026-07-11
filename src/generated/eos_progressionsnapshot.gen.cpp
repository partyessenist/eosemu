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

#include "eos_progressionsnapshot.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_ProgressionSnapshot_BeginSnapshot(EOS_HProgressionSnapshot Handle, const EOS_ProgressionSnapshot_BeginSnapshotOptions* Options, uint32_t* OutSnapshotId)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_ProgressionSnapshot_AddProgression(EOS_HProgressionSnapshot Handle, const EOS_ProgressionSnapshot_AddProgressionOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_ProgressionSnapshot_SubmitSnapshot(EOS_HProgressionSnapshot Handle, const EOS_ProgressionSnapshot_SubmitSnapshotOptions* Options, void* ClientData, const EOS_ProgressionSnapshot_OnSubmitSnapshotCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_ProgressionSnapshot_EndSnapshot(EOS_HProgressionSnapshot Handle, const EOS_ProgressionSnapshot_EndSnapshotOptions* Options)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_ProgressionSnapshot_DeleteSnapshot(EOS_HProgressionSnapshot Handle, const EOS_ProgressionSnapshot_DeleteSnapshotOptions* Options, void* ClientData, const EOS_ProgressionSnapshot_OnDeleteSnapshotCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	EOSEMU_STUB();
	::EOSEmu::StubComplete(CompletionDelegate, ClientData);
}
