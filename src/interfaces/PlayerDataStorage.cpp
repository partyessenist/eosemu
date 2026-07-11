//
// PlayerDataStorage -- real per-user file store with read-back.
//
// WriteFile drives the caller's write-data callback to pull chunks and stores
// the assembled file; ReadFile pushes a stored file back through the read-data
// callback. The transfer runs on the Tick thread via the dispatcher, honouring
// the callback rule.
//
// The store is in-memory by default. With `[Storage] Persist = true` in the
// config and a CacheDirectory in EOS_Platform_Options, every write/delete is
// mirrored to <CacheDirectory>/eosemu_pds/<user>/<file> and loaded back on the
// next run, so player data survives restarts.
//
// Transfer-request handles are reference-counted through a registry:
// EOS_PlayerDataStorageFileTransferRequest_Release before the transfer's Tick
// merely drops the app's reference while the pending completion keeps its own,
// so a release-before-Tick cannot leave the callback writing freed memory.
//

#include "interfaces/Interfaces.h"

#include "core/Config.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/Platform.h"

#include "eos_playerdatastorage_types.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace EOSEmu
{
	namespace
	{
		std::mutex g_Mutex;
		// productId -> (filename -> bytes)
		std::map<std::string, std::map<std::string, std::vector<uint8_t>>> g_Files;

		// One transfer request. The registry entry holds the app's reference
		// (dropped by *_Release); pending transfer lambdas hold their own, so
		// the object outlives whichever side finishes last.
		struct PdsRequest { std::string Filename; EOS_EResult State = EOS_EResult::EOS_Success; };
		std::map<PdsRequest*, std::shared_ptr<PdsRequest>> g_Requests;

		Dispatcher* Dispatch() { Platform* P = CurrentPlatform(); return P ? &P->Dispatch() : nullptr; }

		// The EOS service caps a PlayerDataStorage file at 200MB; a write that
		// exceeds it fails with EOS_PlayerDataStorage_FileSizeTooLarge rather
		// than silently truncating.
		constexpr uint64_t kMaxFileBytes = 200ull * 1024 * 1024;

		std::shared_ptr<PdsRequest> NewRequest(const char* Filename)
		{
			auto Req = std::make_shared<PdsRequest>();
			Req->Filename = Filename ? Filename : "";
			std::lock_guard<std::mutex> Lock(g_Mutex);
			g_Requests[Req.get()] = Req;
			return Req;
		}

		std::shared_ptr<PdsRequest> FindRequest(EOS_HPlayerDataStorageFileTransferRequest H)
		{
			std::lock_guard<std::mutex> Lock(g_Mutex);
			auto It = g_Requests.find(reinterpret_cast<PdsRequest*>(H));
			return It != g_Requests.end() ? It->second : nullptr;
		}

		// --- optional on-disk persistence ([Storage] Persist = true) --------

		namespace fs = std::filesystem;

		bool IsPlainChar(char C)
		{
			return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z')
				|| (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-';
		}

		// EOS filenames may contain '/' and other separators; percent-encode
		// anything that is not filesystem-safe so one stored file maps to one
		// flat on-disk name, reversibly.
		std::string EncodeName(const std::string& Name)
		{
			static const char kHex[] = "0123456789ABCDEF";
			std::string Out;
			Out.reserve(Name.size());
			for (char C : Name)
			{
				if (IsPlainChar(C) && C != '%')
				{
					Out.push_back(C);
				}
				else
				{
					Out.push_back('%');
					Out.push_back(kHex[(static_cast<unsigned char>(C) >> 4) & 0xF]);
					Out.push_back(kHex[static_cast<unsigned char>(C) & 0xF]);
				}
			}
			return Out;
		}

		std::string DecodeName(const std::string& Encoded)
		{
			auto Nib = [](char C) -> int
			{
				if (C >= '0' && C <= '9') return C - '0';
				if (C >= 'A' && C <= 'F') return C - 'A' + 10;
				if (C >= 'a' && C <= 'f') return C - 'a' + 10;
				return -1;
			};
			std::string Out;
			Out.reserve(Encoded.size());
			for (size_t i = 0; i < Encoded.size(); ++i)
			{
				if (Encoded[i] == '%' && i + 2 < Encoded.size()
					&& Nib(Encoded[i + 1]) >= 0 && Nib(Encoded[i + 2]) >= 0)
				{
					Out.push_back(static_cast<char>((Nib(Encoded[i + 1]) << 4) | Nib(Encoded[i + 2])));
					i += 2;
				}
				else
				{
					Out.push_back(Encoded[i]);
				}
			}
			return Out;
		}

		// Root of the on-disk mirror, or empty when persistence is off (no
		// config key, or no CacheDirectory to anchor it to).
		std::string PersistRoot()
		{
			Platform* P = CurrentPlatform();
			if (P == nullptr || !P->Config().GetBool("Storage", "Persist", false))
			{
				return {};
			}
			const std::string& Cache = P->CacheDirectory();
			if (Cache.empty())
			{
				return {};
			}
			std::string Root = Cache;
			if (Root.back() != '\\' && Root.back() != '/') Root += '/';
			return Root + "eosemu_pds";
		}

		// Loads the on-disk mirror into g_Files once per root. Caller holds
		// g_Mutex.
		std::string g_LoadedRoot;
		void LoadStoreLocked(const std::string& Root)
		{
			if (Root.empty() || Root == g_LoadedRoot)
			{
				return;
			}
			g_LoadedRoot = Root;
			std::error_code Ec;
			for (const auto& UserDir : fs::directory_iterator(Root, Ec))
			{
				if (!UserDir.is_directory(Ec)) continue;
				const std::string User = DecodeName(UserDir.path().filename().string());
				for (const auto& File : fs::directory_iterator(UserDir.path(), Ec))
				{
					if (!File.is_regular_file(Ec)) continue;
					const std::string Name = DecodeName(File.path().filename().string());
					std::ifstream In(File.path(), std::ios::binary);
					if (!In) continue;
					std::vector<uint8_t> Bytes((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
					// In-memory state (a write this run) wins over the mirror.
					auto& Table = g_Files[User];
					if (Table.find(Name) == Table.end())
					{
						Table[Name] = std::move(Bytes);
					}
				}
			}
		}

		// Ensures the mirror is loaded (if enabled). Caller holds g_Mutex.
		void EnsureLoadedLocked()
		{
			LoadStoreLocked(PersistRoot());
		}

		// Mirrors one file to disk. Caller holds g_Mutex.
		void PersistWriteLocked(const std::string& User, const std::string& Name, const std::vector<uint8_t>& Bytes)
		{
			const std::string Root = PersistRoot();
			if (Root.empty()) return;
			std::error_code Ec;
			const fs::path Dir = fs::path(Root) / EncodeName(User);
			fs::create_directories(Dir, Ec);
			std::ofstream Out(Dir / EncodeName(Name), std::ios::binary | std::ios::trunc);
			if (Out)
			{
				Out.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			}
			else
			{
				EOSEMU_WARN(PlayerDataStorage, "persist: cannot write '%s'", Name.c_str());
			}
		}

		// Removes one file from the mirror. Caller holds g_Mutex.
		void PersistDeleteLocked(const std::string& User, const std::string& Name)
		{
			const std::string Root = PersistRoot();
			if (Root.empty()) return;
			std::error_code Ec;
			fs::remove(fs::path(Root) / EncodeName(User) / EncodeName(Name), Ec);
		}

		// Filename validity per eos_playerdatastorage_types.h (max 64 bytes).
		EOS_EResult CheckFilename(const char* Filename)
		{
			if (Filename == nullptr || Filename[0] == '\0')
			{
				return EOS_EResult::EOS_PlayerDataStorage_FilenameInvalid;
			}
			if (std::strlen(Filename) > EOS_PLAYERDATASTORAGE_FILENAME_MAX_LENGTH_BYTES)
			{
				return EOS_EResult::EOS_PlayerDataStorage_FilenameLengthInvalid;
			}
			return EOS_EResult::EOS_Success;
		}
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_PlayerDataStorage_QueryFile(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_QueryFileOptions* Options, void* ClientData, const EOS_PlayerDataStorage_OnQueryFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionCallback == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	std::string File = (Options && Options->Filename) ? Options->Filename : "";
	D->Post([CompletionCallback, ClientData, Local, File]
	{
		bool Exists = false;
		{
			std::lock_guard<std::mutex> Lock(g_Mutex);
			EnsureLoadedLocked();
			auto It = g_Files.find(Ids::ProductString(Local));
			Exists = It != g_Files.end() && It->second.count(File) > 0;
		}
		EOS_PlayerDataStorage_QueryFileCallbackInfo Info = {};
		Info.ResultCode = Exists ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NotFound;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		CompletionCallback(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_PlayerDataStorage_QueryFileList(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_QueryFileListOptions* Options, void* ClientData, const EOS_PlayerDataStorage_OnQueryFileListCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionCallback == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	D->Post([CompletionCallback, ClientData, Local]
	{
		uint32_t Count = 0;
		{
			std::lock_guard<std::mutex> Lock(g_Mutex);
			EnsureLoadedLocked();
			auto It = g_Files.find(Ids::ProductString(Local));
			if (It != g_Files.end()) Count = static_cast<uint32_t>(It->second.size());
		}
		EOS_PlayerDataStorage_QueryFileListCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.FileCount = Count;
		CompletionCallback(&Info);
	});
}

namespace
{
	EOS_PlayerDataStorage_FileMetadata* MakeMeta(const std::string& Filename, uint32_t Size)
	{
		EOS_PlayerDataStorage_FileMetadata* M = AllocApi<EOS_PlayerDataStorage_FileMetadata>();
		M->ApiVersion = EOS_PLAYERDATASTORAGE_FILEMETADATA_API_LATEST;
		M->FileSizeBytes = Size;
		M->MD5Hash = AttachString(M, "");
		M->Filename = AttachString(M, Filename.c_str());
		M->LastModifiedTime = 0;
		M->UnencryptedDataSizeBytes = Size;
		return M;
	}
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorage_CopyFileMetadataByFilename(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_CopyFileMetadataByFilenameOptions* Options, EOS_PlayerDataStorage_FileMetadata** OutMetadata)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutMetadata == nullptr || Options->Filename == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutMetadata = nullptr;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	EnsureLoadedLocked();
	auto It = g_Files.find(Ids::ProductString(Options->LocalUserId));
	if (It == g_Files.end()) return EOS_EResult::EOS_NotFound;
	auto Fit = It->second.find(Options->Filename);
	if (Fit == It->second.end()) return EOS_EResult::EOS_NotFound;
	*OutMetadata = MakeMeta(Fit->first, static_cast<uint32_t>(Fit->second.size()));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorage_GetFileMetadataCount(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_GetFileMetadataCountOptions* Options, int32_t* OutFileMetadataCount)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutFileMetadataCount == nullptr) return EOS_EResult::EOS_InvalidParameters;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	EnsureLoadedLocked();
	auto It = g_Files.find(Ids::ProductString(Options->LocalUserId));
	*OutFileMetadataCount = It != g_Files.end() ? static_cast<int32_t>(It->second.size()) : 0;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorage_CopyFileMetadataAtIndex(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_CopyFileMetadataAtIndexOptions* Options, EOS_PlayerDataStorage_FileMetadata** OutMetadata)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (Options == nullptr || OutMetadata == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutMetadata = nullptr;
	std::lock_guard<std::mutex> Lock(g_Mutex);
	EnsureLoadedLocked();
	auto It = g_Files.find(Ids::ProductString(Options->LocalUserId));
	if (It == g_Files.end() || Options->Index >= static_cast<uint32_t>(It->second.size())) return EOS_EResult::EOS_NotFound;
	auto Fit = It->second.begin();
	std::advance(Fit, Options->Index);
	*OutMetadata = MakeMeta(Fit->first, static_cast<uint32_t>(Fit->second.size()));
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_PlayerDataStorage_DuplicateFile(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_DuplicateFileOptions* Options, void* ClientData, const EOS_PlayerDataStorage_OnDuplicateFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionCallback == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_EResult Result = EOS_EResult::EOS_NotFound;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	if (Options != nullptr && Options->SourceFilename != nullptr && Options->DestinationFilename != nullptr)
	{
		std::lock_guard<std::mutex> Lock(g_Mutex);
		EnsureLoadedLocked();
		const std::string User = Ids::ProductString(Local);
		auto& Table = g_Files[User];
		auto Src = Table.find(Options->SourceFilename);
		if (Src != Table.end())
		{
			Table[Options->DestinationFilename] = Src->second;
			PersistWriteLocked(User, Options->DestinationFilename, Src->second);
			Result = EOS_EResult::EOS_Success;
		}
	}
	D->Post([CompletionCallback, ClientData, Local, Result]
	{
		EOS_PlayerDataStorage_DuplicateFileCallbackInfo Info = {};
		Info.ResultCode = Result; Info.ClientData = ClientData; Info.LocalUserId = Local;
		CompletionCallback(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_PlayerDataStorage_DeleteFile(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_DeleteFileOptions* Options, void* ClientData, const EOS_PlayerDataStorage_OnDeleteFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionCallback == nullptr) return;
	Dispatcher* D = Dispatch(); if (!D) return;
	EOS_EResult Result = EOS_EResult::EOS_NotFound;
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	if (Options != nullptr && Options->Filename != nullptr)
	{
		std::lock_guard<std::mutex> Lock(g_Mutex);
		EnsureLoadedLocked();
		const std::string User = Ids::ProductString(Local);
		auto It = g_Files.find(User);
		if (It != g_Files.end() && It->second.erase(Options->Filename) > 0)
		{
			PersistDeleteLocked(User, Options->Filename);
			Result = EOS_EResult::EOS_Success;
		}
	}
	D->Post([CompletionCallback, ClientData, Local, Result]
	{
		EOS_PlayerDataStorage_DeleteFileCallbackInfo Info = {};
		Info.ResultCode = Result; Info.ClientData = ClientData; Info.LocalUserId = Local;
		CompletionCallback(&Info);
	});
}

EOS_DECLARE_FUNC(EOS_HPlayerDataStorageFileTransferRequest) EOS_PlayerDataStorage_ReadFile(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_ReadFileOptions* ReadOptions, void* ClientData, const EOS_PlayerDataStorage_OnReadFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (ReadOptions == nullptr || ReadOptions->Filename == nullptr) return nullptr;
	std::shared_ptr<PdsRequest> Req = NewRequest(ReadOptions->Filename);

	EOS_ProductUserId Local = ReadOptions->LocalUserId;
	auto DataCb = ReadOptions->ReadFileDataCallback;
	std::string Filename = ReadOptions->Filename;
	Dispatcher* D = Dispatch();
	if (D != nullptr)
	{
		D->Post([Req, CompletionCallback, ClientData, Local, DataCb, Filename]
		{
			std::vector<uint8_t> Data;
			bool Found = false;
			{
				std::lock_guard<std::mutex> Lock(g_Mutex);
				EnsureLoadedLocked();
				auto It = g_Files.find(Ids::ProductString(Local));
				if (It != g_Files.end()) { auto Fit = It->second.find(Filename); if (Fit != It->second.end()) { Data = Fit->second; Found = true; } }
			}
			EOS_EResult Result = EOS_EResult::EOS_NotFound;
			if (Found && DataCb != nullptr)
			{
				EOS_PlayerDataStorage_ReadFileDataCallbackInfo Info = {};
				Info.ClientData = ClientData;
				Info.LocalUserId = Local;
				Info.Filename = Filename.c_str();
				Info.TotalFileSizeBytes = static_cast<uint32_t>(Data.size());
				Info.bIsLastChunk = EOS_TRUE;
				Info.DataChunkLengthBytes = static_cast<uint32_t>(Data.size());
				Info.DataChunk = Data.empty() ? nullptr : Data.data();
				// Whole file in one chunk; the callback can still veto it.
				switch (DataCb(&Info))
				{
				case EOS_PlayerDataStorage_EReadResult::EOS_RR_FailRequest:
					Result = EOS_EResult::EOS_PlayerDataStorage_UserErrorFromDataCallback; break;
				case EOS_PlayerDataStorage_EReadResult::EOS_RR_CancelRequest:
					Result = EOS_EResult::EOS_Canceled; break;
				default:
					Result = EOS_EResult::EOS_Success; break;
				}
			}
			else if (Found)
			{
				Result = EOS_EResult::EOS_Success;
			}
			{
				std::lock_guard<std::mutex> Lock(g_Mutex);
				Req->State = Result;
			}
			if (CompletionCallback != nullptr)
			{
				EOS_PlayerDataStorage_ReadFileCallbackInfo CInfo = {};
				CInfo.ResultCode = Result; CInfo.ClientData = ClientData; CInfo.LocalUserId = Local; CInfo.Filename = Filename.c_str();
				CompletionCallback(&CInfo);
			}
		});
	}
	return reinterpret_cast<EOS_HPlayerDataStorageFileTransferRequest>(Req.get());
}

EOS_DECLARE_FUNC(EOS_HPlayerDataStorageFileTransferRequest) EOS_PlayerDataStorage_WriteFile(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_WriteFileOptions* WriteOptions, void* ClientData, const EOS_PlayerDataStorage_OnWriteFileCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (WriteOptions == nullptr || WriteOptions->Filename == nullptr) return nullptr;
	const EOS_EResult NameCheck = CheckFilename(WriteOptions->Filename);
	std::shared_ptr<PdsRequest> Req = NewRequest(WriteOptions->Filename);

	EOS_ProductUserId Local = WriteOptions->LocalUserId;
	auto DataCb = WriteOptions->WriteFileDataCallback;
	uint32_t Chunk = WriteOptions->ChunkLengthBytes ? WriteOptions->ChunkLengthBytes : 4096;
	std::string Filename = WriteOptions->Filename;
	Dispatcher* D = Dispatch();
	if (D != nullptr)
	{
		D->Post([Req, CompletionCallback, ClientData, Local, DataCb, Chunk, Filename, NameCheck]
		{
			std::vector<uint8_t> Assembled;
			EOS_EResult Result = NameCheck;
			if (Result == EOS_EResult::EOS_Success && DataCb != nullptr)
			{
				std::vector<uint8_t> Buffer(Chunk);
				bool Done = false;
				while (!Done)
				{
					EOS_PlayerDataStorage_WriteFileDataCallbackInfo Info = {};
					Info.ClientData = ClientData;
					Info.LocalUserId = Local;
					Info.Filename = Filename.c_str();
					Info.DataBufferLengthBytes = Chunk;
					uint32_t Written = 0;
					EOS_PlayerDataStorage_EWriteResult WR = DataCb(&Info, Buffer.data(), &Written);
					if (Written > 0) Assembled.insert(Assembled.end(), Buffer.begin(), Buffer.begin() + Written);
					if (Assembled.size() > kMaxFileBytes)
					{
						// Refuse over-size writes outright; storing a truncated
						// file as Success would be a silent data loss.
						Result = EOS_EResult::EOS_PlayerDataStorage_FileSizeTooLarge;
						break;
					}
					switch (WR)
					{
					case EOS_PlayerDataStorage_EWriteResult::EOS_WR_ContinueWriting: break;
					case EOS_PlayerDataStorage_EWriteResult::EOS_WR_CompleteRequest: Done = true; break;
					case EOS_PlayerDataStorage_EWriteResult::EOS_WR_FailRequest:
						Done = true; Result = EOS_EResult::EOS_PlayerDataStorage_UserErrorFromDataCallback; break;
					default: Done = true; Result = EOS_EResult::EOS_Canceled; break;
					}
				}
			}
			if (Result == EOS_EResult::EOS_Success)
			{
				std::lock_guard<std::mutex> Lock(g_Mutex);
				EnsureLoadedLocked();
				const std::string User = Ids::ProductString(Local);
				PersistWriteLocked(User, Filename, Assembled);
				g_Files[User][Filename] = std::move(Assembled);
			}
			{
				std::lock_guard<std::mutex> Lock(g_Mutex);
				Req->State = Result;
			}
			if (CompletionCallback != nullptr)
			{
				EOS_PlayerDataStorage_WriteFileCallbackInfo CInfo = {};
				CInfo.ResultCode = Result; CInfo.ClientData = ClientData; CInfo.LocalUserId = Local; CInfo.Filename = Filename.c_str();
				CompletionCallback(&CInfo);
			}
		});
	}
	return reinterpret_cast<EOS_HPlayerDataStorageFileTransferRequest>(Req.get());
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorage_DeleteCache(EOS_HPlayerDataStorage Handle, const EOS_PlayerDataStorage_DeleteCacheOptions* Options, void* ClientData, const EOS_PlayerDataStorage_OnDeleteCacheCompleteCallback CompletionCallback)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	// DeleteCache removes locally cached copies of remote data. Our store *is*
	// the authority (there is no remote), so deleting it would destroy player
	// data; report success without touching anything.
	EOS_ProductUserId Local = Options ? Options->LocalUserId : nullptr;
	if (CompletionCallback != nullptr)
	{
		if (Dispatcher* D = Dispatch())
		{
			D->Post([CompletionCallback, ClientData, Local]
			{
				EOS_PlayerDataStorage_DeleteCacheCallbackInfo Info = {};
				Info.ResultCode = EOS_EResult::EOS_Success; Info.ClientData = ClientData; Info.LocalUserId = Local;
				CompletionCallback(&Info);
			});
		}
	}
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorageFileTransferRequest_GetFileRequestState(EOS_HPlayerDataStorageFileTransferRequest Handle)
{
	EOSEMU_API_TRACE();
	std::lock_guard<std::mutex> Lock(g_Mutex);
	auto It = g_Requests.find(reinterpret_cast<PdsRequest*>(Handle));
	return It != g_Requests.end() ? It->second->State : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorageFileTransferRequest_GetFilename(EOS_HPlayerDataStorageFileTransferRequest Handle, uint32_t FilenameStringBufferSizeBytes, char* OutStringBuffer, int32_t* OutStringLength)
{
	EOSEMU_API_TRACE();
	std::shared_ptr<PdsRequest> Req = FindRequest(Handle);
	if (Req == nullptr || OutStringLength == nullptr) return EOS_EResult::EOS_InvalidParameters;
	*OutStringLength = static_cast<int32_t>(Req->Filename.size());
	if (OutStringBuffer == nullptr || FilenameStringBufferSizeBytes < Req->Filename.size() + 1) return EOS_EResult::EOS_LimitExceeded;
	std::memcpy(OutStringBuffer, Req->Filename.c_str(), Req->Filename.size() + 1);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_PlayerDataStorageFileTransferRequest_CancelRequest(EOS_HPlayerDataStorageFileTransferRequest Handle)
{
	EOSEMU_API_TRACE();
	std::shared_ptr<PdsRequest> Req = FindRequest(Handle);
	// Transfers complete on the next Tick, so by the time a caller cancels the
	// request has usually already finished.
	return Req != nullptr ? EOS_EResult::EOS_NoChange : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_PlayerDataStorageFileTransferRequest_Release(EOS_HPlayerDataStorageFileTransferRequest Handle)
{
	EOSEMU_API_TRACE();
	// Drops the registry's (the app's) reference. A transfer still pending on
	// the dispatcher holds its own shared_ptr, so this can never free memory
	// the completion lambda is about to write.
	std::lock_guard<std::mutex> Lock(g_Mutex);
	g_Requests.erase(reinterpret_cast<PdsRequest*>(Handle));
}

EOS_DECLARE_FUNC(void) EOS_PlayerDataStorage_FileMetadata_Release(EOS_PlayerDataStorage_FileMetadata* FileMetadata)
{
	EOSEMU_API_TRACE();
	FreeApiBlock(FileMetadata);
}
