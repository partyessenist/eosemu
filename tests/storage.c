// PlayerDataStorage test: transfer-request lifetime safety, filename
// validation, and (with `[Storage] Persist = true` + a CacheDirectory) the
// on-disk mirror surviving a process restart.
//
// Modes:
//   storage.exe write <cachedir>  -- writes save/slot1.dat (10000 patterned
//       bytes) and temp.bin, deletes temp.bin, probes release-before-Tick and
//       an over-long filename, exits.
//   storage.exe read <cachedir>   -- fresh process: asserts slot1 reads back
//       byte-identical from the mirror and temp.bin stays deleted.

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	define SLEEP_MS(ms) Sleep(ms)
#else
#	include <time.h>
static void SLEEP_MS(int ms) { struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L}; nanosleep(&ts, NULL); }
#endif

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_logging.h"
#include "eos_playerdatastorage.h"

#define FILE_BYTES 10000
#define FILE_NAME "save/slot1.dat"

static EOS_HPlatform gPlatform;
static EOS_HPlayerDataStorage gPds;
static EOS_ProductUserId gLocalPuid;
static int gConnectDone;

static int gChecks, gFails;
static void Check(int ok, const char* what)
{
	++gChecks;
	if (!ok) { ++gFails; printf("  FAIL: %s\n", what); }
	else printf("  ok: %s\n", what);
}

static void Tick(int frames)
{
	for (int i = 0; i < frames; ++i) { EOS_Platform_Tick(gPlatform); SLEEP_MS(20); }
}

static void EOS_CALL OnConnect(const EOS_Connect_LoginCallbackInfo* D)
{
	gLocalPuid = D->LocalUserId;
	gConnectDone = 1;
}

static unsigned char PatternAt(int i) { return (unsigned char)((i * 31 + 7) & 0xFF); }

/* ---- write-side driver: feeds FILE_BYTES in 1024-byte chunks ---- */
static int gWriteOffset;
static EOS_PlayerDataStorage_EWriteResult EOS_CALL WriteData(const EOS_PlayerDataStorage_WriteFileDataCallbackInfo* Info, void* OutBuffer, uint32_t* OutWritten)
{
	int remain = FILE_BYTES - gWriteOffset;
	int put = remain < (int)Info->DataBufferLengthBytes ? remain : (int)Info->DataBufferLengthBytes;
	for (int i = 0; i < put; ++i) ((unsigned char*)OutBuffer)[i] = PatternAt(gWriteOffset + i);
	gWriteOffset += put;
	*OutWritten = (uint32_t)put;
	return gWriteOffset >= FILE_BYTES ? EOS_WR_CompleteRequest : EOS_WR_ContinueWriting;
}

static EOS_PlayerDataStorage_EWriteResult EOS_CALL WriteTiny(const EOS_PlayerDataStorage_WriteFileDataCallbackInfo* Info, void* OutBuffer, uint32_t* OutWritten)
{
	(void)Info;
	memcpy(OutBuffer, "tiny", 4);
	*OutWritten = 4;
	return EOS_WR_CompleteRequest;
}

static EOS_EResult gLastResult = EOS_UnexpectedError;
static int gCompletions;
static void EOS_CALL OnWriteDone(const EOS_PlayerDataStorage_WriteFileCallbackInfo* D) { gLastResult = D->ResultCode; ++gCompletions; }
static void EOS_CALL OnDeleteDone(const EOS_PlayerDataStorage_DeleteFileCallbackInfo* D) { gLastResult = D->ResultCode; ++gCompletions; }
static void EOS_CALL OnQueryDone(const EOS_PlayerDataStorage_QueryFileCallbackInfo* D) { gLastResult = D->ResultCode; ++gCompletions; }

/* ---- read-side: collect and verify the pattern ---- */
static int gReadBytes, gReadPatternOk;
static EOS_PlayerDataStorage_EReadResult EOS_CALL ReadData(const EOS_PlayerDataStorage_ReadFileDataCallbackInfo* Info)
{
	const unsigned char* p = (const unsigned char*)Info->DataChunk;
	for (uint32_t i = 0; i < Info->DataChunkLengthBytes; ++i)
		if (p[i] != PatternAt(gReadBytes + i)) { gReadPatternOk = 0; return EOS_RR_FailRequest; }
	gReadBytes += (int)Info->DataChunkLengthBytes;
	return EOS_RR_ContinueReading;
}
static void EOS_CALL OnReadDone(const EOS_PlayerDataStorage_ReadFileCallbackInfo* D) { gLastResult = D->ResultCode; ++gCompletions; }

static int WaitCompletion(int prev)
{
	for (int i = 0; i < 250 && gCompletions == prev; ++i) Tick(1);
	return gCompletions > prev;
}

int main(int argc, char** argv)
{
	const char* mode = argc > 1 ? argv[1] : "write";
	const char* cache = argc > 2 ? argv[2] : NULL;
	int isWrite = strcmp(mode, "write") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuStorage"; io.ProductVersion = "1.0";
	if (EOS_Initialize(&io) != EOS_Success) { printf("init failed\n"); return 1; }

	EOS_Platform_Options po; memset(&po, 0, sizeof(po));
	po.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	po.ProductId = "prod"; po.SandboxId = "sand"; po.DeploymentId = "deploy";
	po.ClientCredentials.ClientId = "cid"; po.ClientCredentials.ClientSecret = "secret";
	po.CacheDirectory = cache;
	gPlatform = EOS_Platform_Create(&po);
	if (!gPlatform) { printf("platform create failed\n"); EOS_Shutdown(); return 2; }

	gPds = EOS_Platform_GetPlayerDataStorageInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);
	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);

	if (isWrite)
	{
		/* 1. Write the main save file. */
		gWriteOffset = 0;
		EOS_PlayerDataStorage_WriteFileOptions wo = {0};
		wo.ApiVersion = EOS_PLAYERDATASTORAGE_WRITEFILEOPTIONS_API_LATEST;
		wo.LocalUserId = gLocalPuid;
		wo.Filename = FILE_NAME;
		wo.ChunkLengthBytes = 1024;
		wo.WriteFileDataCallback = &WriteData;
		int prev = gCompletions;
		EOS_HPlayerDataStorageFileTransferRequest req = EOS_PlayerDataStorage_WriteFile(gPds, &wo, NULL, &OnWriteDone);
		Check(req != NULL, "WriteFile returned a request");
		Check(WaitCompletion(prev) && gLastResult == EOS_Success, "write slot1 completed Success");
		Check(EOS_PlayerDataStorageFileTransferRequest_GetFileRequestState(req) == EOS_Success, "request state Success");
		char nameBuf[128]; int32_t nameLen = 0;
		Check(EOS_PlayerDataStorageFileTransferRequest_GetFilename(req, sizeof(nameBuf), nameBuf, &nameLen) == EOS_Success
			&& strcmp(nameBuf, FILE_NAME) == 0, "request filename round-trips");
		EOS_PlayerDataStorageFileTransferRequest_Release(req);
		Check(EOS_PlayerDataStorageFileTransferRequest_GetFileRequestState(req) == EOS_InvalidParameters,
			"released handle is invalid");

		/* 2. Write + delete a second file. */
		EOS_PlayerDataStorage_WriteFileOptions w2 = {0};
		w2.ApiVersion = EOS_PLAYERDATASTORAGE_WRITEFILEOPTIONS_API_LATEST;
		w2.LocalUserId = gLocalPuid;
		w2.Filename = "temp.bin";
		w2.ChunkLengthBytes = 64;
		w2.WriteFileDataCallback = &WriteTiny;
		prev = gCompletions;
		EOS_HPlayerDataStorageFileTransferRequest r2 = EOS_PlayerDataStorage_WriteFile(gPds, &w2, NULL, &OnWriteDone);
		Check(WaitCompletion(prev) && gLastResult == EOS_Success, "write temp.bin completed Success");
		EOS_PlayerDataStorageFileTransferRequest_Release(r2);

		EOS_PlayerDataStorage_DeleteFileOptions dl = {0};
		dl.ApiVersion = EOS_PLAYERDATASTORAGE_DELETEFILEOPTIONS_API_LATEST;
		dl.LocalUserId = gLocalPuid;
		dl.Filename = "temp.bin";
		prev = gCompletions;
		EOS_PlayerDataStorage_DeleteFile(gPds, &dl, NULL, &OnDeleteDone);
		Check(WaitCompletion(prev) && gLastResult == EOS_Success, "delete temp.bin completed Success");

		/* 3. Release-before-Tick must not crash nor lose the completion. */
		gReadBytes = 0; gReadPatternOk = 1;
		EOS_PlayerDataStorage_ReadFileOptions ro = {0};
		ro.ApiVersion = EOS_PLAYERDATASTORAGE_READFILEOPTIONS_API_LATEST;
		ro.LocalUserId = gLocalPuid;
		ro.Filename = FILE_NAME;
		ro.ReadChunkLengthBytes = 4096;
		ro.ReadFileDataCallback = &ReadData;
		prev = gCompletions;
		EOS_HPlayerDataStorageFileTransferRequest r3 = EOS_PlayerDataStorage_ReadFile(gPds, &ro, NULL, &OnReadDone);
		EOS_PlayerDataStorageFileTransferRequest_Release(r3); /* before any Tick */
		Check(WaitCompletion(prev) && gLastResult == EOS_Success, "read after early release still completes");
		Check(gReadBytes == FILE_BYTES && gReadPatternOk, "read-back matches in-process");

		/* 4. Over-long filename is rejected, not stored. */
		char longName[80];
		memset(longName, 'a', sizeof(longName) - 1);
		longName[sizeof(longName) - 1] = 0;
		EOS_PlayerDataStorage_WriteFileOptions w4 = {0};
		w4.ApiVersion = EOS_PLAYERDATASTORAGE_WRITEFILEOPTIONS_API_LATEST;
		w4.LocalUserId = gLocalPuid;
		w4.Filename = longName;
		w4.ChunkLengthBytes = 64;
		w4.WriteFileDataCallback = &WriteTiny;
		prev = gCompletions;
		EOS_HPlayerDataStorageFileTransferRequest r4 = EOS_PlayerDataStorage_WriteFile(gPds, &w4, NULL, &OnWriteDone);
		Check(WaitCompletion(prev) && gLastResult == EOS_PlayerDataStorage_FilenameLengthInvalid,
			"over-long filename rejected");
		EOS_PlayerDataStorageFileTransferRequest_Release(r4);
	}
	else
	{
		/* Fresh process: the mirror must supply slot1 and not temp.bin. */
		gReadBytes = 0; gReadPatternOk = 1;
		EOS_PlayerDataStorage_ReadFileOptions ro = {0};
		ro.ApiVersion = EOS_PLAYERDATASTORAGE_READFILEOPTIONS_API_LATEST;
		ro.LocalUserId = gLocalPuid;
		ro.Filename = FILE_NAME;
		ro.ReadChunkLengthBytes = 4096;
		ro.ReadFileDataCallback = &ReadData;
		int prev = gCompletions;
		EOS_HPlayerDataStorageFileTransferRequest req = EOS_PlayerDataStorage_ReadFile(gPds, &ro, NULL, &OnReadDone);
		Check(WaitCompletion(prev) && gLastResult == EOS_Success, "persisted slot1 reads back");
		Check(gReadBytes == FILE_BYTES && gReadPatternOk, "persisted bytes match the pattern");
		if (req) EOS_PlayerDataStorageFileTransferRequest_Release(req);

		EOS_PlayerDataStorage_QueryFileOptions qf = {0};
		qf.ApiVersion = EOS_PLAYERDATASTORAGE_QUERYFILEOPTIONS_API_LATEST;
		qf.LocalUserId = gLocalPuid;
		qf.Filename = "temp.bin";
		prev = gCompletions;
		EOS_PlayerDataStorage_QueryFile(gPds, &qf, NULL, &OnQueryDone);
		Check(WaitCompletion(prev) && gLastResult == EOS_NotFound, "deleted temp.bin stays deleted");
	}

	EOS_Platform_Release(gPlatform);
	EOS_Shutdown();
	printf("STORAGE-%s %s (%d/%d)\n", isWrite ? "WRITE" : "READ", gFails == 0 ? "PASS" : "FAIL", gChecks - gFails, gChecks);
	return gFails == 0 ? 0 : 3;
}
