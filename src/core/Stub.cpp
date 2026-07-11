#include "core/Stub.h"

#include "core/Logging.h"
#include "core/Platform.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_set>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace EOSEmu
{
	namespace
	{
		std::mutex& StubMutex()
		{
			static std::mutex Mutex;
			return Mutex;
		}

		std::unordered_set<std::string>& SeenStubs()
		{
			static std::unordered_set<std::string> Seen;
			return Seen;
		}

		bool TraceEveryCall()
		{
			static const bool bEnabled = []
			{
#if defined(_MSC_VER)
				// getenv is deprecated under MSVC's secure-CRT warnings.
				size_t Length = 0;
				char Buffer[8] = {};
				if (getenv_s(&Length, Buffer, sizeof(Buffer), "EOSEMU_TRACE") != 0 || Length == 0)
				{
					return false;
				}
				return Buffer[0] != '0';
#else
				const char* Value = std::getenv("EOSEMU_TRACE");
				return Value != nullptr && Value[0] != '0';
#endif
			}();
			return bEnabled;
		}

		void Emit(const char* Function)
		{
			std::fprintf(stderr, "[EOSEmu] stub: %s\n", Function);
			std::fflush(stderr);
#if defined(_WIN32)
			// Games are usually windowed with no console; the debugger sees this.
			OutputDebugStringA("[EOSEmu] stub: ");
			OutputDebugStringA(Function);
			OutputDebugStringA("\n");
#endif
			// Also surface through the consumer's own log sink -- the fastest way
			// for a game's developer to see which stubbed surface it depends on
			// (CLAUDE.md, "Log every stub hit once, at EOS_LOG_Warning").
			EOSEMU_WARN(Core, "unimplemented entry point called: %s", Function);
		}
	}

	bool PostStubTask(std::function<void()> Task)
	{
		Platform* P = CurrentPlatform();
		if (P == nullptr)
		{
			return false;
		}
		P->Dispatch().Post(std::move(Task));
		return true;
	}

	void TraceStub(const char* Function)
	{
		if (TraceEveryCall())
		{
			Emit(Function);
			return;
		}

		{
			std::lock_guard<std::mutex> Lock(StubMutex());
			if (!SeenStubs().insert(Function).second)
			{
				return;
			}
		}

		Emit(Function);
	}
}
