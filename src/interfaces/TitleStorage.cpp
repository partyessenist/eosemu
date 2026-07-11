//
// TitleStorage -- read-only title-provisioned files, served from a local folder.
//
// Title files are provisioned server-side; a game that gates features on them
// (e.g. a co-op menu enabled only by an `is_multiplayer_enabled` flag in a
// live-ops config file) sees EOS_NotFound and disables those features. So we
// serve them from a directory:
//
//   1. [TitleStorage] Dir in eosemu.ini (absolute path), else
//   2. <CacheDirectory>/eosemu_titlestorage/ when the game supplies one, else
//   3. eosemu_titlestorage/ relative to the working directory.
//
// Drop the plaintext files (captured with EOSTracer, or otherwise) in that
// folder. Files are served as-is: EOS's on-the-wire encryption is transparent to
// the consumer, which only ever sees the bytes we hand its read callback.
//

#include "interfaces/Interfaces.h"
#include "core/Logging.h"

#include "core/Config.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_titlestorage_types.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace EOSEmu
{
	namespace
	{
		struct TsRequest { std::string Filename; EOS_EResult State = EOS_EResult::EOS_NotFound; };

		Dispatcher* Dispatch() { Platform* P = CurrentPlatform(); return P ? &P->Dispatch() : nullptr; }

		std::mutex g_Mutex;
		// Files confirmed present by a prior QueryFile/QueryFileList, in order --
		// backs GetFileMetadataCount / CopyFileMetadataAtIndex.
		std::vector<std::string> g_Known;

		void RememberLocked(const std::string& Filename)
		{
			for (const std::string& F : g_Known)
			{
				if (F == Filename) return;
			}
			g_Known.push_back(Filename);
		}

		// Directory we serve from, ending in a separator. See file header.
		std::string TitleStorageDir()
		{
			std::string Dir;
			if (Platform* P = CurrentPlatform())
			{
				Dir = P->Config().GetString("TitleStorage", "Dir");
				if (Dir.empty() && !P->CacheDirectory().empty())
				{
					Dir = P->CacheDirectory() + "/eosemu_titlestorage";
				}
			}
			if (Dir.empty())
			{
				Dir = "eosemu_titlestorage";
			}
			if (Dir.back() != '\\' && Dir.back() != '/')
			{
				Dir += '/';
			}
			return Dir;
		}

		// Reject any name carrying a path separator so a served name can't escape
		// the directory.
		bool SafeName(const std::string& Filename)
		{
			return !Filename.empty() && Filename.find_first_of("\\/:") == std::string::npos
				&& Filename.find("..") == std::string::npos;
		}

		bool ReadServedFile(const std::string& Filename, std::vector<uint8_t>& Out)
		{
			if (!SafeName(Filename)) return false;
			std::ifstream In(TitleStorageDir() + Filename, std::ios::binary);
			if (!In) return false;
			Out.assign(std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>());
			return true;
		}

		bool ServedFileExists(const std::string& Filename)
		{
			if (!SafeName(Filename)) return false;
			std::ifstream In(TitleStorageDir() + Filename, std::ios::binary);
			return static_cast<bool>(In);
		}

		// A stable 32-hex identifier for the file, reported as MD5Hash. It is not a
		// real MD5 -- the real SDK's hash is of the encrypted backend blob, and the
		// consumer does not verify the delivered plaintext against it (the real SDK
		// delivers fewer bytes than the hashed size). Content-derived so it changes
		// when the file does.
		std::string ContentTag(const std::vector<uint8_t>& Data)
		{
			auto Fnv = [&Data](uint64_t Seed) {
				uint64_t H = Seed;
				for (uint8_t B : Data) { H ^= B; H *= 1099511628211ull; }
				return H;
			};
			char Out[33];
			std::snprintf(Out, sizeof(Out), "%016llx%016llx",
				static_cast<unsigned long long>(Fnv(14695981039346656037ull)),
				static_cast<unsigned long long>(Fnv(1469598103934665603ull)));
			return std::string(Out, 32);
		}

		EOS_TitleStorage_FileMetadata* MakeMetadata(const std::string& Filename)
		{
			std::vector<uint8_t> Data;
			if (!ReadServedFile(Filename, Data)) return nullptr;
			EOS_TitleStorage_FileMetadata* M = AllocApi<EOS_TitleStorage_FileMetadata>();
			M->ApiVersion = EOS_TITLESTORAGE_FILEMETADATA_API_LATEST;
			M->FileSizeBytes = static_cast<uint32_t>(Data.size());
			M->UnencryptedDataSizeBytes = static_cast<uint32_t>(Data.size());
			M->Filename = AttachString(M, Filename.c_str());
			M->MD5Hash = AttachString(M, ContentTag(Data).c_str());
			return M;
		}
	}
}

using namespace EOSEmu;

namespace { TsRequest* AsReq(EOS_HTitleStorageFileTransferRequest H) { return reinterpret_cast<TsRequest*>(H); } }

EOS_DECLARE_FUNC(void) EOS_TitleStorage_QueryFile(EOS_HTitleStorage Handle, const EOS_TitleStorage_QueryFileOptions* Options, void* ClientData, const EOS_TitleStorage_OnQueryFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionCallback == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	std::string Filename = (Options && Options->Filename) ? Options->Filename : std::string();
	D->Post([CompletionCallback, ClientData, Local, Filename]
	{
		const bool Exists = ServedFileExists(Filename);
		EOSEMU_INFO(TitleStorage, "QueryFile '%s' -> %s (serving from %s)",
			Filename.c_str(), Exists ? "found" : "NOT FOUND", TitleStorageDir().c_str());
		if (Exists)
		{
			std::lock_guard<std::mutex> Lock(g_Mutex);
			RememberLocked(Filename);
		}
		EOS_TitleStorage_QueryFileCallbackInfo Info = {};
		Info.ResultCode = Exists ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NotFound;
		Info.ClientData = ClientData; Info.LocalUserId = Local;
		CompletionCallback(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_TitleStorage_QueryFileList(EOS_HTitleStorage Handle, const EOS_TitleStorage_QueryFileListOptions* Options, void* ClientData, const EOS_TitleStorage_OnQueryFileListCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionCallback == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	// We do not enumerate the directory: a game reaches files by name (via its own
	// manifest). Report the count already confirmed by QueryFile so an app mixing
	// the two sees a consistent list. Always succeeds.
	D->Post([CompletionCallback, ClientData, Local]
	{
		uint32_t Count;
		{ std::lock_guard<std::mutex> Lock(g_Mutex); Count = static_cast<uint32_t>(g_Known.size()); }
		EOS_TitleStorage_QueryFileListCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData; Info.LocalUserId = Local; Info.FileCount = Count;
		CompletionCallback(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_TitleStorage_CopyFileMetadataByFilename(EOS_HTitleStorage Handle, const EOS_TitleStorage_CopyFileMetadataByFilenameOptions* Options, EOS_TitleStorage_FileMetadata** OutMetadata)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (OutMetadata == nullptr || Options == nullptr || Options->Filename == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutMetadata = MakeMetadata(Options->Filename);
	return *OutMetadata != nullptr ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(uint32_t) EOS_TitleStorage_GetFileMetadataCount(EOS_HTitleStorage Handle, const EOS_TitleStorage_GetFileMetadataCountOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	return static_cast<uint32_t>(g_Known.size());
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_TitleStorage_CopyFileMetadataAtIndex(EOS_HTitleStorage Handle, const EOS_TitleStorage_CopyFileMetadataAtIndexOptions* Options, EOS_TitleStorage_FileMetadata** OutMetadata)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (OutMetadata == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	std::string Filename;
	{
		std::lock_guard<std::mutex> Lock(g_Mutex);
		if (Options->Index >= g_Known.size()) return EOS_EResult::EOS_NotFound;
		Filename = g_Known[Options->Index];
	}
	*OutMetadata = MakeMetadata(Filename);
	return *OutMetadata != nullptr ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_HTitleStorageFileTransferRequest) EOS_TitleStorage_ReadFile(EOS_HTitleStorage Handle, const EOS_TitleStorage_ReadFileOptions* ReadOptions, void* ClientData, const EOS_TitleStorage_OnReadFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (ReadOptions == nullptr || ReadOptions->Filename == nullptr) return nullptr;
	auto* Req = new TsRequest();
	Req->Filename = ReadOptions->Filename;

	EOS_ProductUserId Local = ReadOptions->LocalUserId;
	std::string Filename = ReadOptions->Filename;
	auto DataCb = ReadOptions->ReadFileDataCallback;
	auto ProgressCb = ReadOptions->FileTransferProgressCallback;

	if (Dispatcher* D = Dispatch())
	{
		D->Post([Req, CompletionCallback, ClientData, Local, Filename, DataCb, ProgressCb]
		{
			std::vector<uint8_t> Data;
			const bool Found = ReadServedFile(Filename, Data);
			EOS_EResult Result = EOS_EResult::EOS_NotFound;
			if (Found)
			{
				const uint32_t Size = static_cast<uint32_t>(Data.size());
				// Progress first (games mirror the real download's 100% update).
				if (ProgressCb != nullptr)
				{
					EOS_TitleStorage_FileTransferProgressCallbackInfo PInfo = {};
					PInfo.ClientData = ClientData; PInfo.LocalUserId = Local; PInfo.Filename = Filename.c_str();
					PInfo.BytesTransferred = Size; PInfo.TotalFileSizeBytes = Size;
					ProgressCb(&PInfo);
				}
				Result = EOS_EResult::EOS_Success;
				if (DataCb != nullptr)
				{
					EOS_TitleStorage_ReadFileDataCallbackInfo Info = {};
					Info.ClientData = ClientData; Info.LocalUserId = Local; Info.Filename = Filename.c_str();
					Info.TotalFileSizeBytes = Size;
					Info.bIsLastChunk = EOS_TRUE;             // small config files: one chunk
					Info.DataChunkLengthBytes = Size;
					Info.DataChunk = Data.empty() ? nullptr : Data.data();
					switch (DataCb(&Info))
					{
					case EOS_TitleStorage_EReadResult::EOS_TS_RR_FailRequest: Result = EOS_EResult::EOS_UnexpectedError; break;
					case EOS_TitleStorage_EReadResult::EOS_TS_RR_CancelRequest: Result = EOS_EResult::EOS_Canceled; break;
					default: break; // ContinueReading: whole file already delivered
					}
				}
				std::lock_guard<std::mutex> Lock(g_Mutex);
				RememberLocked(Filename);
			}
			Req->State = Result;
			if (CompletionCallback != nullptr)
			{
				EOS_TitleStorage_ReadFileCallbackInfo CInfo = {};
				CInfo.ResultCode = Result; CInfo.ClientData = ClientData; CInfo.LocalUserId = Local; CInfo.Filename = Filename.c_str();
				CompletionCallback(&CInfo);
			}
		});
	}
	return reinterpret_cast<EOS_HTitleStorageFileTransferRequest>(Req);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_TitleStorage_DeleteCache(EOS_HTitleStorage Handle, const EOS_TitleStorage_DeleteCacheOptions* Options, void* ClientData, const EOS_TitleStorage_OnDeleteCacheCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	if (CompletionCallback != nullptr)
	{
		if (Dispatcher* D = Dispatch())
		{
			D->Post([CompletionCallback, ClientData, Local]
			{
				EOS_TitleStorage_DeleteCacheCallbackInfo Info = {};
				Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData; Info.LocalUserId = Local;
				CompletionCallback(&Info);
			});
		}
	}
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_TitleStorageFileTransferRequest_GetFileRequestState(EOS_HTitleStorageFileTransferRequest Handle)
{
	EOSEMU_API_TRACE();
	auto* Req = AsReq(Handle);
	return Req ? Req->State : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_TitleStorageFileTransferRequest_GetFilename(EOS_HTitleStorageFileTransferRequest Handle, uint32_t FilenameStringBufferSizeBytes, char* OutStringBuffer, int32_t* OutStringLength)
{
	EOSEMU_API_TRACE();
	auto* Req = AsReq(Handle);
	if (Req == nullptr || OutStringLength == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutStringLength = static_cast<int32_t>(Req->Filename.size());
	if (OutStringBuffer == nullptr || FilenameStringBufferSizeBytes < Req->Filename.size() + 1) return EOS_EResult::EOS_LimitExceeded;
	std::memcpy(OutStringBuffer, Req->Filename.c_str(), Req->Filename.size() + 1);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_TitleStorageFileTransferRequest_CancelRequest(EOS_HTitleStorageFileTransferRequest Handle)
{
	EOSEMU_API_TRACE();
	auto* Req = AsReq(Handle);
	return Req ? EOS_EResult::EOS_NoChange : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_TitleStorageFileTransferRequest_Release(EOS_HTitleStorageFileTransferRequest Handle)
{
	EOSEMU_API_TRACE();
	delete AsReq(Handle);
}

EOS_DECLARE_FUNC(void) EOS_TitleStorage_FileMetadata_Release(EOS_TitleStorage_FileMetadata* FileMetadata)
{
	EOSEMU_API_TRACE();
	EOSEmu::FreeApiBlock(FileMetadata);
}
