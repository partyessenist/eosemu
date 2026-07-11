#pragma once

//
// Deferred callback dispatch.
//
// Consumer callbacks must fire from inside EOS_Platform_Tick, on the caller's
// thread -- the samples mutate shared state from completion callbacks without
// any lock (Friends.cpp:635 -> push_back at :296), which is only safe if we
// never call back from a worker thread. Our LAN transport runs on a background
// thread, so it enqueues closures here and Tick drains them. See CLAUDE.md,
// "Callbacks must be dispatched from inside EOS_Platform_Tick".
//

#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

namespace EOSEmu
{
	class Dispatcher
	{
	public:
		using Task = std::function<void()>;
		using Clock = std::chrono::steady_clock;

		/// Queues a closure to run on the next Tick. Thread-safe; typically
		/// called from the LAN receive thread.
		void Post(Task Work)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Pending_.push_back(std::move(Work));
		}

		/// Queues a closure to run on the first Tick at least `DelayMs` after
		/// now. Real EOS async ops complete after backend latency, never on the
		/// same frame; some consumers rely on that gap. One shipped game's host
		/// flow issues EOS_LobbySearch_Find and only copies the result inside the
		/// completion callback -- if the completion fires too soon (before its
		/// coroutine finishes suspending to receive it), it races and copies a
		/// null handle, failing to host. A small delay reproduces the backend
		/// gap the game expects. Thread-safe.
		void PostAfter(unsigned DelayMs, Task Work)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Delayed_.push_back({Clock::now() + std::chrono::milliseconds(DelayMs), std::move(Work)});
		}

		/// Runs due closures on the calling thread, in FIFO order. A closure
		/// queued during draining runs on the following Tick, not this one, so a
		/// self-reposting task cannot starve the frame.
		void Drain()
		{
			std::vector<Task> Ready;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				Ready.swap(Pending_);
				const auto Now = Clock::now();
				for (auto It = Delayed_.begin(); It != Delayed_.end();)
				{
					if (It->Due <= Now) { Ready.push_back(std::move(It->Work)); It = Delayed_.erase(It); }
					else { ++It; }
				}
			}
			for (Task& Work : Ready)
			{
				Work();
			}
		}

	private:
		struct DelayedTask { Clock::time_point Due; Task Work; };
		std::mutex Mutex_;
		std::vector<Task> Pending_;
		std::vector<DelayedTask> Delayed_;
	};
}
