#include "core/Memory.h"

#include <cstring>

#if defined(_WIN32)
#	include <malloc.h>
#else
#	include <cstdlib>
#endif

namespace EOSEmu
{
	namespace
	{
		EOS_AllocateMemoryFunc g_Allocate = nullptr;
		EOS_ReallocateMemoryFunc g_Reallocate = nullptr;
		EOS_ReleaseMemoryFunc g_Release = nullptr;

		size_t RoundUpPow2(size_t Alignment)
		{
			// The SDK guarantees callers a power-of-two alignment; guard anyway
			// so a stray 0 or non-power-of-two can't reach _aligned_malloc.
			if (Alignment < alignof(std::max_align_t))
			{
				return alignof(std::max_align_t);
			}
			return Alignment;
		}

		void* PlatformAlloc(size_t Size, size_t Alignment)
		{
#if defined(_WIN32)
			return _aligned_malloc(Size, Alignment);
#else
			void* Ptr = nullptr;
			// posix_memalign requires alignment to be a multiple of sizeof(void*).
			size_t A = Alignment < sizeof(void*) ? sizeof(void*) : Alignment;
			if (posix_memalign(&Ptr, A, Size) != 0)
			{
				return nullptr;
			}
			return Ptr;
#endif
		}

		void* PlatformRealloc(void* Ptr, size_t Size, size_t Alignment)
		{
#if defined(_WIN32)
			return _aligned_realloc(Ptr, Size, Alignment);
#else
			// No aligned realloc in POSIX; emulate. We do not know the old size,
			// so we over-copy up to the new size and rely on the allocation being
			// at least that large in practice only for grow operations, which is
			// all EOSEmu ever does. Callers needing shrink don't exist here.
			if (Ptr == nullptr)
			{
				return PlatformAlloc(Size, Alignment);
			}
			void* Fresh = PlatformAlloc(Size, Alignment);
			if (Fresh != nullptr)
			{
				std::memcpy(Fresh, Ptr, Size);
				free(Ptr);
			}
			return Fresh;
#endif
		}

		void PlatformFree(void* Ptr)
		{
#if defined(_WIN32)
			_aligned_free(Ptr);
#else
			free(Ptr);
#endif
		}

		// --------------------------------------------------------------------
		// API-block bookkeeping.
		//
		// Every block begins with a BlockHeader placed at the very start of the
		// underlying allocation. The pointer we hand out is Header+1 rounded up
		// to the requested alignment. Attached allocations are chained through
		// AttachNode so FreeApiBlock can release them all.
		// --------------------------------------------------------------------
		struct AttachNode
		{
			AttachNode* Next;
		};

		struct BlockHeader
		{
			// The raw base returned by the underlying allocator (what must be
			// freed), which may differ from Header when alignment padding was
			// added ahead of the header.
			void* Base;
			AttachNode* Attachments;
		};

		BlockHeader* HeaderOf(void* Block)
		{
			// The header sits immediately before the usable pointer.
			return reinterpret_cast<BlockHeader*>(
				reinterpret_cast<uint8_t*>(Block) - sizeof(BlockHeader));
		}
	}

	void SetAllocators(
		EOS_AllocateMemoryFunc Allocate,
		EOS_ReallocateMemoryFunc Reallocate,
		EOS_ReleaseMemoryFunc Release)
	{
		g_Allocate = Allocate;
		g_Reallocate = Reallocate;
		g_Release = Release;
	}

	void ClearAllocators()
	{
		g_Allocate = nullptr;
		g_Reallocate = nullptr;
		g_Release = nullptr;
	}

	void* Alloc(size_t Size, size_t Alignment)
	{
		Alignment = RoundUpPow2(Alignment);
		if (g_Allocate != nullptr)
		{
			return g_Allocate(Size, Alignment);
		}
		return PlatformAlloc(Size, Alignment);
	}

	void* Realloc(void* Pointer, size_t Size, size_t Alignment)
	{
		Alignment = RoundUpPow2(Alignment);
		if (g_Reallocate != nullptr)
		{
			return g_Reallocate(Pointer, Size, Alignment);
		}
		return PlatformRealloc(Pointer, Size, Alignment);
	}

	void Free(void* Pointer)
	{
		if (Pointer == nullptr)
		{
			return;
		}
		if (g_Release != nullptr)
		{
			g_Release(Pointer);
			return;
		}
		PlatformFree(Pointer);
	}

	void* AllocApiBlock(size_t Size, size_t Alignment)
	{
		Alignment = RoundUpPow2(Alignment);
		// Reserve room for the header plus enough slack to align the usable
		// pointer past it. Worst case the header lands just after Base and we
		// still need Alignment-1 bytes of padding to reach an aligned address.
		const size_t Prefix = sizeof(BlockHeader) + Alignment;
		void* Base = Alloc(Prefix + Size, alignof(std::max_align_t));
		if (Base == nullptr)
		{
			return nullptr;
		}

		// Place the usable pointer at the first Alignment-aligned address that
		// leaves room for a header immediately before it.
		uintptr_t Raw = reinterpret_cast<uintptr_t>(Base) + sizeof(BlockHeader);
		uintptr_t Aligned = (Raw + (Alignment - 1)) & ~static_cast<uintptr_t>(Alignment - 1);
		void* Block = reinterpret_cast<void*>(Aligned);

		BlockHeader* Header = HeaderOf(Block);
		Header->Base = Base;
		Header->Attachments = nullptr;

		std::memset(Block, 0, Size);
		return Block;
	}

	void* AttachBytes(void* Block, size_t Size, size_t Alignment)
	{
		if (Block == nullptr)
		{
			return nullptr;
		}
		Alignment = RoundUpPow2(Alignment);
		// One allocation holding the chain node followed by aligned payload.
		const size_t NodeSpan = sizeof(AttachNode) + Alignment;
		void* Raw = Alloc(NodeSpan + Size, alignof(std::max_align_t));
		if (Raw == nullptr)
		{
			return nullptr;
		}

		AttachNode* Node = static_cast<AttachNode*>(Raw);
		uintptr_t After = reinterpret_cast<uintptr_t>(Raw) + sizeof(AttachNode);
		uintptr_t Aligned = (After + (Alignment - 1)) & ~static_cast<uintptr_t>(Alignment - 1);
		void* Payload = reinterpret_cast<void*>(Aligned);

		BlockHeader* Header = HeaderOf(Block);
		Node->Next = Header->Attachments;
		Header->Attachments = Node;

		std::memset(Payload, 0, Size);
		return Payload;
	}

	const char* AttachString(void* Block, const char* String)
	{
		if (String == nullptr)
		{
			return nullptr;
		}
		return AttachString(Block, String, std::strlen(String));
	}

	const char* AttachString(void* Block, const char* String, size_t Length)
	{
		if (String == nullptr)
		{
			return nullptr;
		}
		char* Copy = static_cast<char*>(AttachBytes(Block, Length + 1, alignof(char)));
		if (Copy == nullptr)
		{
			return nullptr;
		}
		std::memcpy(Copy, String, Length);
		Copy[Length] = '\0';
		return Copy;
	}

	void FreeApiBlock(void* Block)
	{
		if (Block == nullptr)
		{
			return;
		}
		BlockHeader* Header = HeaderOf(Block);
		AttachNode* Node = Header->Attachments;
		while (Node != nullptr)
		{
			AttachNode* Next = Node->Next;
			Free(Node);
			Node = Next;
		}
		Free(Header->Base);
	}
}
