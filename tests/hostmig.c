// Two-instance host-migration test.
//
// host: creates a lobby, waits until the joiner is a member, then leaves.
//       Leaving as owner with migration enabled must hand the lobby to the
//       remaining member (a one-shot handoff announce).
// join: joins the lobby, then waits for EOS_LMS_PROMOTED naming itself and
//       verifies it adopted ownership (GetLobbyOwner == self).
//
// Exits 0 only if the role's checks all pass.

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
#include "eos_lobby.h"

static EOS_HPlatform gPlatform;
static EOS_HLobby gLobby;
static EOS_ProductUserId gLocalPuid;

static int gConnectDone;
static char gLobbyId[256];
static int gLobbyCreated;
static int gLobbyJoined;
static int gMemberJoinSeen;   /* host: joiner appeared in the member list */
static int gPromotedSelf;     /* join: EOS_LMS_PROMOTED with TargetUserId == self */
static int gLeaveOk;          /* host: LeaveLobby completed EOS_Success */

static void EOS_CALL LogCb(const EOS_LogMessage* M)
{
	if (M->Level <= EOS_LOG_Warning)
		printf("  [%s] %s\n", M->Category ? M->Category : "?", M->Message ? M->Message : "");
}

static void Tick(int frames)
{
	for (int i = 0; i < frames; ++i)
	{
		EOS_Platform_Tick(gPlatform);
		SLEEP_MS(20);
	}
}

static void EOS_CALL OnConnect(const EOS_Connect_LoginCallbackInfo* D)
{
	gLocalPuid = D->LocalUserId;
	gConnectDone = 1;
}

static void EOS_CALL OnCreateLobby(const EOS_Lobby_CreateLobbyCallbackInfo* D)
{
	if (D->ResultCode == EOS_Success && D->LobbyId)
	{
		strncpy(gLobbyId, D->LobbyId, sizeof(gLobbyId) - 1);
		gLobbyCreated = 1;
		printf("  host: created lobby %s\n", gLobbyId);
	}
}

static void EOS_CALL OnJoinLobby(const EOS_Lobby_JoinLobbyCallbackInfo* D)
{
	if (D->ResultCode == EOS_Success)
	{
		gLobbyJoined = 1;
		if (D->LobbyId) strncpy(gLobbyId, D->LobbyId, sizeof(gLobbyId) - 1);
		printf("  join: joined lobby %s\n", D->LobbyId ? D->LobbyId : "?");
	}
}

static void EOS_CALL OnLeaveLobby(const EOS_Lobby_LeaveLobbyCallbackInfo* D)
{
	gLeaveOk = (D->ResultCode == EOS_Success);
	printf("  host: leave -> %s\n", EOS_EResult_ToString(D->ResultCode));
}

static void EOS_CALL OnMemberStatus(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* D)
{
	if (D->CurrentStatus == EOS_LMS_JOINED && D->TargetUserId != gLocalPuid)
	{
		gMemberJoinSeen = 1;
		printf("  host: member joined\n");
	}
	if (D->CurrentStatus == EOS_LMS_PROMOTED && D->TargetUserId == gLocalPuid)
	{
		gPromotedSelf = 1;
		printf("  join: promoted to owner\n");
	}
}

static EOS_HLobbySearch gSearch;
static void EOS_CALL OnFind(const EOS_LobbySearch_FindCallbackInfo* D)
{
	if (D->ResultCode != EOS_Success) return;
	EOS_LobbySearch_GetSearchResultCountOptions co = {0};
	co.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
	uint32_t n = EOS_LobbySearch_GetSearchResultCount(gSearch, &co);
	if (n > 0)
	{
		EOS_LobbySearch_CopySearchResultByIndexOptions cp = {0};
		cp.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
		cp.LobbyIndex = 0;
		EOS_HLobbyDetails details = NULL;
		if (EOS_LobbySearch_CopySearchResultByIndex(gSearch, &cp, &details) == EOS_Success && details)
		{
			EOS_Lobby_JoinLobbyOptions jo = {0};
			jo.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
			jo.LobbyDetailsHandle = details;
			jo.LocalUserId = gLocalPuid;
			EOS_Lobby_JoinLobby(gLobby, &jo, NULL, &OnJoinLobby);
			EOS_LobbyDetails_Release(details);
		}
	}
}

/* Reads the current owner of gLobbyId through the details handle. */
static int OwnerIsSelf(void)
{
	EOS_Lobby_CopyLobbyDetailsHandleOptions ch = {0};
	ch.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
	ch.LobbyId = gLobbyId;
	ch.LocalUserId = gLocalPuid;
	EOS_HLobbyDetails det = NULL;
	int self = 0;
	if (EOS_Lobby_CopyLobbyDetailsHandle(gLobby, &ch, &det) == EOS_Success && det)
	{
		EOS_LobbyDetails_GetLobbyOwnerOptions go = {0};
		go.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
		self = (EOS_LobbyDetails_GetLobbyOwner(det, &go) == gLocalPuid);
		EOS_LobbyDetails_Release(det);
	}
	return self;
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "host";
	int isHost = strcmp(role, "host") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuHostMig"; io.ProductVersion = "1.0";
	if (EOS_Initialize(&io) != EOS_Success) { printf("init failed\n"); return 1; }
	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Warning);

	EOS_Platform_Options po; memset(&po, 0, sizeof(po));
	po.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	po.ProductId = "prod"; po.SandboxId = "sand"; po.DeploymentId = "deploy";
	po.ClientCredentials.ClientId = "cid"; po.ClientCredentials.ClientSecret = "secret";
	gPlatform = EOS_Platform_Create(&po);
	if (!gPlatform) { printf("platform create failed\n"); EOS_Shutdown(); return 2; }

	gLobby = EOS_Platform_GetLobbyInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);

	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);
	printf("[%s] connected\n", role);

	EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions mso = {0};
	mso.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
	EOS_Lobby_AddNotifyLobbyMemberStatusReceived(gLobby, &mso, NULL, &OnMemberStatus);

	if (isHost)
	{
		EOS_Lobby_CreateLobbyOptions lo; memset(&lo, 0, sizeof(lo));
		lo.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
		lo.LocalUserId = gLocalPuid;
		lo.MaxLobbyMembers = 4;
		lo.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
		lo.bAllowInvites = EOS_TRUE;
		lo.BucketId = "HostMig:Test";
		/* bDisableHostMigration stays 0: migration enabled. */
		EOS_Lobby_CreateLobby(gLobby, &lo, NULL, &OnCreateLobby);

		/* Wait for the joiner to become a member (up to ~20s). */
		for (int i = 0; i < 1000 && !gMemberJoinSeen; ++i) Tick(1);

		if (gMemberJoinSeen)
		{
			EOS_Lobby_LeaveLobbyOptions lv = {0};
			lv.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
			lv.LobbyId = gLobbyId;
			lv.LocalUserId = gLocalPuid;
			EOS_Lobby_LeaveLobby(gLobby, &lv, NULL, &OnLeaveLobby);
			for (int i = 0; i < 100 && !gLeaveOk; ++i) Tick(1);
		}
		/* Linger so the handoff announce is out and answered. */
		Tick(100);

		int ok = gLobbyCreated && gMemberJoinSeen && gLeaveOk;
		printf("[host] created=%d joinSeen=%d leaveOk=%d\n", gLobbyCreated, gMemberJoinSeen, gLeaveOk);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("HOSTMIG-HOST %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else
	{
		/* Give discovery time to see the host, then find + join the lobby. */
		for (int i = 0; i < 150; ++i) Tick(1);

		EOS_Lobby_CreateLobbySearchOptions cso = {0};
		cso.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
		cso.MaxResults = 10;
		if (EOS_Lobby_CreateLobbySearch(gLobby, &cso, &gSearch) == EOS_Success)
		{
			EOS_LobbySearch_SetParameterOptions sp = {0};
			sp.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
			EOS_Lobby_AttributeData ad = {0};
			ad.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
			ad.Key = EOS_LOBBY_SEARCH_BUCKET_ID; ad.ValueType = EOS_AT_STRING; ad.Value.AsUtf8 = "HostMig:Test";
			sp.Parameter = &ad; sp.ComparisonOp = EOS_CO_EQUAL;
			EOS_LobbySearch_SetParameter(gSearch, &sp);

			EOS_LobbySearch_FindOptions fo = {0};
			fo.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
			fo.LocalUserId = gLocalPuid;
			EOS_LobbySearch_Find(gSearch, &fo, NULL, &OnFind);
		}
		for (int i = 0; i < 250 && !gLobbyJoined; ++i) Tick(1);

		/* The host leaves once it sees us; wait for the promotion (~20s). */
		for (int i = 0; i < 1000 && !gPromotedSelf; ++i) Tick(1);

		int ownerOk = gPromotedSelf && OwnerIsSelf();
		int ok = gLobbyJoined && gPromotedSelf && ownerOk;
		printf("[join] joined=%d promoted=%d ownerSelf=%d\n", gLobbyJoined, gPromotedSelf, ownerOk);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("HOSTMIG-JOIN %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
}
