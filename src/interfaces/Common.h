#pragma once

//
// Shared helpers for the interface entry points.
//
// The API boundary is meant to be a thin translation layer: validate
// ApiVersion, marshal, hand off. These utilities keep that boilerplate in one
// place -- notification registries (AddNotify*/RemoveNotify*), notification-id
// minting, and the deferred completion-callback pattern.
//

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "eos_common.h"

namespace EOSEmu
{
	/// ApiVersion gate. Options structs grow append-only, so a caller declaring
	/// version V only allocated the fields introduced at or below V; reading a
	/// later field is an out-of-bounds read. Guard every post-v1 field with
	/// HasField(V, IntroducedAt) and reject versions newer than the 1.16.4
	/// headers we are built from with EOS_IncompatibleVersion.
	inline bool VersionInRange(int32_t V, int32_t Latest) { return V >= 1 && V <= Latest; }
	inline bool HasField(int32_t V, int32_t IntroducedAt) { return V >= IntroducedAt; }

	/// Monotonic, process-unique, never zero -- EOS_INVALID_NOTIFICATIONID is 0.
	inline EOS_NotificationId NextNotificationId()
	{
		static std::atomic<uint64_t> Counter{1};
		return Counter.fetch_add(1);
	}

	/// Monotonic, process-unique, never zero -- EOS_UI_EVENTID_INVALID is 0.
	/// One shared sequence across Lobby + Sessions so a UiEventId names exactly
	/// one pending join across both interfaces (EOS_UI_AcknowledgeEventId can then
	/// release it without knowing which interface minted it). EOS_UI_EventId is
	/// uint64_t (eos_ui_types.h); typed as uint64_t here to avoid the include.
	inline uint64_t NextUiEventId()
	{
		static std::atomic<uint64_t> Counter{1};
		return Counter.fetch_add(1);
	}

	/// A set of registered notification handlers of one callback type. Add
	/// returns a handle to pass back to the matching RemoveNotify*; iterate
	/// Entries to fire them (always from the Tick thread via the Dispatcher).
	template <typename FnPtr>
	class NotifyRegistry
	{
	public:
		struct Entry
		{
			EOS_NotificationId Id;
			FnPtr Fn;
			void* ClientData;
		};

		EOS_NotificationId Add(FnPtr Fn, void* ClientData)
		{
			if (Fn == nullptr)
			{
				return EOS_INVALID_NOTIFICATIONID;
			}
			const EOS_NotificationId Id = NextNotificationId();
			std::lock_guard<std::mutex> Lock(Mutex_);
			Entries_.push_back(Entry{Id, Fn, ClientData});
			return Id;
		}

		void Remove(EOS_NotificationId Id)
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (auto It = Entries_.begin(); It != Entries_.end(); ++It)
			{
				if (It->Id == Id)
				{
					Entries_.erase(It);
					return;
				}
			}
		}

		/// Copy taken under the lock: Add/Remove run on the game thread while
		/// the LAN receive thread iterates to fan notifications out to the
		/// Dispatcher. Iterating the live vector would race a reallocation.
		std::vector<Entry> Snapshot() const
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			return Entries_;
		}

		bool Empty() const
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			return Entries_.empty();
		}

	private:
		mutable std::mutex Mutex_;
		std::vector<Entry> Entries_;
	};
}
