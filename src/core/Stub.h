#pragma once

//
// Support for the generated stub layer.
//
// Every EOS entry point EOSEmu has not implemented yet routes through
// EOSEMU_STUB(). It records the call so that running a real game against the
// emulator tells us exactly which surface that game depends on -- which is far
// cheaper than reading the game's disassembly.
//

#include <functional>

#include "eos_common.h"

// Generated stubs trace their own entry via EOSEMU_API_TRACE() (VeryVerbose);
// pulling Logging.h in here keeps that macro available in every generated TU.
#include "core/Logging.h"

namespace EOSEmu
{
	/// Records that an unimplemented entry point was called.
	/// Reports each distinct function once. Set EOSEMU_TRACE=1 in the
	/// environment to report every call instead.
	void TraceStub(const char* Function);

	/// Queues Task onto the platform dispatcher (fires from EOS_Platform_Tick
	/// on the caller's thread). Returns false when no platform exists.
	bool PostStubTask(std::function<void()> Task);

	/// Completion for a stubbed async operation. An async stub that never
	/// fires its delegate strands any consumer that gates progress on it
	/// (EOS_EResult_IsOperationComplete says "keep waiting"). Info is deduced
	/// from the callback's parameter; every EOS completion Info starts with
	/// ResultCode/ClientData, and the rest stays zeroed exactly like the
	/// hand-written `Info = {}` paths.
	template <typename Info>
	void StubComplete(void (EOS_CALL* Fn)(const Info*), void* ClientData)
	{
		if (Fn == nullptr)
		{
			return;
		}
		PostStubTask([Fn, ClientData]
		{
			Info I = {};
			I.ResultCode = EOS_EResult::EOS_NotImplemented;
			I.ClientData = ClientData;
			Fn(&I);
		});
	}
}

// __FUNCTION__ yields the bare function name on MSVC, GCC and Clang, which
// keeps 623 copies of the name out of the generated sources.
#define EOSEMU_STUB() ::EOSEmu::TraceStub(__FUNCTION__)
