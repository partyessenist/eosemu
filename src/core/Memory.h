#pragma once

//
// Memory for EOSEmu.
//
// EOS_Initialize may hand us an allocate/reallocate/release triple
// (eos_init.h:76-112). Everything the SDK gives to the consumer, and everything
// the consumer gives back through a *_Release, must round-trip through that same
// triple. When the consumer passes nullptr for all three (as every sample does)
// we fall back to the platform's aligned allocator.
//
// On top of that sits the "API block": the allocation shape that the
// Copy*/`*_Release` contract needs. A Copy* out-struct is a fixed-size struct
// whose `const char*` and array members point at separately sized data, but
// *_Release only ever receives the struct pointer. So a block carries a hidden
// header immediately before the struct recording every attached allocation, and
// FreeApiBlock walks it. See CLAUDE.md, "Copy* allocates, Get* borrows".
//

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "eos_init.h"

namespace EOSEmu
{
	/// Installs the consumer's allocator triple. All three must be null (use the
	/// platform allocator) or all three non-null; eos_init.h requires it and
	/// EOS_Initialize rejects the mixed case before calling this.
	void SetAllocators(
		EOS_AllocateMemoryFunc Allocate,
		EOS_ReallocateMemoryFunc Reallocate,
		EOS_ReleaseMemoryFunc Release);

	/// Restores the platform allocator. Called from EOS_Shutdown.
	void ClearAllocators();

	/// Alignment must be a power of two, as the SDK promises its allocators.
	void* Alloc(size_t Size, size_t Alignment = alignof(std::max_align_t));
	void* Realloc(void* Pointer, size_t Size, size_t Alignment = alignof(std::max_align_t));
	void Free(void* Pointer);

	// ------------------------------------------------------------------------
	// API blocks
	// ------------------------------------------------------------------------

	/// Allocates a zeroed block of `Size` bytes that FreeApiBlock can release
	/// along with anything later attached to it. Returns the usable pointer,
	/// not the header.
	void* AllocApiBlock(size_t Size, size_t Alignment);

	/// Allocates `Size` bytes whose lifetime is tied to `Block`.
	void* AttachBytes(void* Block, size_t Size, size_t Alignment = alignof(std::max_align_t));

	/// Copies `String` into storage owned by `Block`. Null in, null out --
	/// EOS represents "no value" as a null `const char*`, not as "".
	const char* AttachString(void* Block, const char* String);
	const char* AttachString(void* Block, const char* String, size_t Length);

	/// Frees a block and every allocation attached to it. Null is a no-op, so
	/// this is safe to call straight from a *_Release entry point.
	void FreeApiBlock(void* Block);

	/// Allocates one zero-initialised T as an API block.
	template <typename T>
	T* AllocApi()
	{
		static_assert(std::is_trivially_copyable<T>::value,
			"API structs are C structs; a non-trivial type here means the wrong T.");
		return static_cast<T*>(AllocApiBlock(sizeof(T), alignof(T)));
	}

	/// Allocates a zeroed array of `Count` T attached to `Block`.
	template <typename T>
	T* AttachArray(void* Block, size_t Count)
	{
		if (Count == 0)
		{
			return nullptr;
		}
		return static_cast<T*>(AttachBytes(Block, sizeof(T) * Count, alignof(T)));
	}
}
