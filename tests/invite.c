// Two-instance overlay invite-accept / join-friend test.
//
// Exercises the LAN wiring behind the social overlay's accept flow. Because a
// headless test cannot click the overlay, EOSEmu exposes the same routing via
// EOSEMU_OVERLAY_AUTOACCEPT (auto-accept every received invite) and
// EOSEMU_OVERLAY_AUTOJOIN (auto-join every discovered game). Those synthesize
// exactly the actions an overlay button would, so the InviteAccepted /
// JoinLobbyAccepted callbacks and the UiEventId resolution are what actually run.
//
// Roles:
//   host    : creates a lobby ("InviteTest:Room"). If it discovers a guest
//             lobby ("InviteTest:Guest"), it sends that guest an invite to the
//             room. Otherwise it just advertises the room.
//   invitee : creates the guest lobby (so the host can find + target it), sets
//             AUTOACCEPT. On EOS_Lobby_OnLobbyInviteAccepted it resolves the
//             invite (CopyLobbyDetailsHandleByInviteId) and joins the room.
//             PASS = invite received + accepted + joined a lobby != its own.
//   joiner  : creates no lobby, sets AUTOJOIN. On EOS_Lobby_OnJoinLobbyAccepted
//             it resolves the UiEventId (CopyLobbyDetailsHandleByUiEventId),
//             joins, and acknowledges the event. PASS = join-accepted + joined.
//
// Exits 0 only if the role's checks pass.

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
#include "eos_ui.h"

static EOS_HPlatform gPlatform;
static EOS_HLobby gLobby;
static EOS_HUI gUI;
static EOS_ProductUserId gLocalPuid;

static int gConnectDone;
static char gOwnLobbyId[256];   /* the lobby this process created (if any) */
static char gJoinedLobbyId[256];
static int gOwnLobbyCreated;
static int gJoined;
static int gInviteReceived;
static int gInviteAccepted;
static int gJoinAccepted;       /* joiner: JoinLobbyAccepted fired */
static int gInviteSent;         /* host: sent an invite to a discovered guest */

static void EOS_CALL LogCb(const EOS_LogMessage* M)
{
	if (M->Level <= EOS_LOG_Warning)
		printf("  [%s] %s\n", M->Category ? M->Category : "?", M->Message ? M->Message : "");
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

static void EOS_CALL OnCreateLobby(const EOS_Lobby_CreateLobbyCallbackInfo* D)
{
	if (D->ResultCode == EOS_Success && D->LobbyId)
	{
		strncpy(gOwnLobbyId, D->LobbyId, sizeof(gOwnLobbyId) - 1);
		gOwnLobbyCreated = 1;
	}
}

static void EOS_CALL OnJoinLobby(const EOS_Lobby_JoinLobbyCallbackInfo* D)
{
	if (D->ResultCode == EOS_Success && D->LobbyId)
	{
		strncpy(gJoinedLobbyId, D->LobbyId, sizeof(gJoinedLobbyId) - 1);
		gJoined = 1;
		printf("  joined lobby %s\n", D->LobbyId);
	}
}

/* invitee: an invite was accepted (auto or by overlay) -> resolve + join. */
static void EOS_CALL OnInviteAccepted(const EOS_Lobby_LobbyInviteAcceptedCallbackInfo* D)
{
	gInviteAccepted = 1;
	printf("  invite accepted (id=%s)\n", D->InviteId ? D->InviteId : "?");
	EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions co = {0};
	co.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYINVITEID_API_LATEST;
	co.InviteId = D->InviteId;
	EOS_HLobbyDetails det = NULL;
	if (EOS_Lobby_CopyLobbyDetailsHandleByInviteId(gLobby, &co, &det) == EOS_Success && det)
	{
		EOS_Lobby_JoinLobbyOptions jo = {0};
		jo.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
		jo.LobbyDetailsHandle = det;
		jo.LocalUserId = gLocalPuid;
		EOS_Lobby_JoinLobby(gLobby, &jo, NULL, &OnJoinLobby);
		EOS_LobbyDetails_Release(det);
	}
}

static void EOS_CALL OnInviteReceived(const EOS_Lobby_LobbyInviteReceivedCallbackInfo* D)
{
	(void)D;
	gInviteReceived = 1;
	printf("  invite received\n");
}

/* joiner: JoinLobbyAccepted carries a UiEventId -> resolve + join + ack. */
static void EOS_CALL OnJoinLobbyAccepted(const EOS_Lobby_JoinLobbyAcceptedCallbackInfo* D)
{
	gJoinAccepted = 1;
	printf("  join-lobby accepted (event=%llu)\n", (unsigned long long)D->UiEventId);
	EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions co = {0};
	co.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYUIEVENTID_API_LATEST;
	co.UiEventId = D->UiEventId;
	EOS_HLobbyDetails det = NULL;
	if (EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(gLobby, &co, &det) == EOS_Success && det)
	{
		EOS_Lobby_JoinLobbyOptions jo = {0};
		jo.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
		jo.LobbyDetailsHandle = det;
		jo.LocalUserId = gLocalPuid;
		EOS_Lobby_JoinLobby(gLobby, &jo, NULL, &OnJoinLobby);
		EOS_LobbyDetails_Release(det);
	}
	EOS_UI_AcknowledgeEventIdOptions ao = {0};
	ao.ApiVersion = EOS_UI_ACKNOWLEDGEEVENTID_API_LATEST;
	ao.UiEventId = D->UiEventId;
	ao.Result = EOS_Success;
	EOS_UI_AcknowledgeEventId(gUI, &ao);
}

/* host: search for the invitee's guest lobby, and invite its owner to the room. */
static EOS_HLobbySearch gSearch;
static void EOS_CALL OnFind(const EOS_LobbySearch_FindCallbackInfo* D)
{
	if (D->ResultCode != EOS_Success || gInviteSent) return;
	EOS_LobbySearch_GetSearchResultCountOptions co = {0};
	co.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
	if (EOS_LobbySearch_GetSearchResultCount(gSearch, &co) == 0) return;
	EOS_LobbySearch_CopySearchResultByIndexOptions cp = {0};
	cp.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
	cp.LobbyIndex = 0;
	EOS_HLobbyDetails det = NULL;
	if (EOS_LobbySearch_CopySearchResultByIndex(gSearch, &cp, &det) != EOS_Success || !det) return;
	EOS_LobbyDetails_GetLobbyOwnerOptions go = {0};
	go.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
	EOS_ProductUserId guest = EOS_LobbyDetails_GetLobbyOwner(det, &go);
	EOS_LobbyDetails_Release(det);
	if (guest && gOwnLobbyId[0])
	{
		EOS_Lobby_SendInviteOptions so = {0};
		so.ApiVersion = EOS_LOBBY_SENDINVITE_API_LATEST;
		so.LobbyId = gOwnLobbyId;
		so.LocalUserId = gLocalPuid;
		so.TargetUserId = guest;
		EOS_Lobby_SendInvite(gLobby, &so, NULL, NULL);
		gInviteSent = 1;
		printf("  host: sent invite to guest\n");
	}
}

static void CreateOwnLobby(const char* bucket)
{
	EOS_Lobby_CreateLobbyOptions lo; memset(&lo, 0, sizeof(lo));
	lo.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
	lo.LocalUserId = gLocalPuid;
	lo.MaxLobbyMembers = 4;
	lo.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
	lo.bAllowInvites = EOS_TRUE;
	lo.BucketId = bucket;
	EOS_Lobby_CreateLobby(gLobby, &lo, NULL, &OnCreateLobby);
	for (int i = 0; i < 50 && !gOwnLobbyCreated; ++i) Tick(1);
}

static void StartGuestLobbySearch(void)
{
	EOS_Lobby_CreateLobbySearchOptions cso = {0};
	cso.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
	cso.MaxResults = 10;
	if (EOS_Lobby_CreateLobbySearch(gLobby, &cso, &gSearch) != EOS_Success) return;
	EOS_LobbySearch_SetParameterOptions sp = {0};
	sp.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
	EOS_Lobby_AttributeData ad = {0};
	ad.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
	ad.Key = EOS_LOBBY_SEARCH_BUCKET_ID; ad.ValueType = EOS_AT_STRING; ad.Value.AsUtf8 = "InviteTest:Guest";
	sp.Parameter = &ad; sp.ComparisonOp = EOS_CO_EQUAL;
	EOS_LobbySearch_SetParameter(gSearch, &sp);
	EOS_LobbySearch_FindOptions fo = {0};
	fo.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
	fo.LocalUserId = gLocalPuid;
	EOS_LobbySearch_Find(gSearch, &fo, NULL, &OnFind);
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "host";
	const int isHost = strcmp(role, "host") == 0;
	const int isInvitee = strcmp(role, "invitee") == 0;
	const int isJoiner = strcmp(role, "joiner") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuInvite"; io.ProductVersion = "1.0";
	if (EOS_Initialize(&io) != EOS_Success) { printf("init failed\n"); return 1; }
	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Warning);

	EOS_Platform_Options po; memset(&po, 0, sizeof(po));
	po.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	po.ProductId = "prod"; po.SandboxId = "sand"; po.DeploymentId = "deploy";
	po.ClientCredentials.ClientId = "cid"; po.ClientCredentials.ClientSecret = "secret";
	/* Disable the overlay window: the auto hooks drive the same routing headless. */
	po.Flags = EOS_PF_DISABLE_OVERLAY;
	gPlatform = EOS_Platform_Create(&po);
	if (!gPlatform) { printf("platform create failed\n"); EOS_Shutdown(); return 2; }

	gLobby = EOS_Platform_GetLobbyInterface(gPlatform);
	gUI = EOS_Platform_GetUIInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);

	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);
	printf("[%s] connected\n", role);

	/* Everyone listens for invites/join events; only the relevant ones fire. */
	EOS_Lobby_AddNotifyLobbyInviteReceivedOptions iro = {0};
	iro.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYINVITERECEIVED_API_LATEST;
	EOS_Lobby_AddNotifyLobbyInviteReceived(gLobby, &iro, NULL, &OnInviteReceived);
	EOS_Lobby_AddNotifyLobbyInviteAcceptedOptions iao = {0};
	iao.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYINVITEACCEPTED_API_LATEST;
	EOS_Lobby_AddNotifyLobbyInviteAccepted(gLobby, &iao, NULL, &OnInviteAccepted);
	EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions jao = {0};
	jao.ApiVersion = EOS_LOBBY_ADDNOTIFYJOINLOBBYACCEPTED_API_LATEST;
	EOS_Lobby_AddNotifyJoinLobbyAccepted(gLobby, &jao, NULL, &OnJoinLobbyAccepted);

	if (isHost)
	{
		CreateOwnLobby("InviteTest:Room");
		for (int i = 0; i < 150; ++i) Tick(1);   /* let peers/guest lobbies appear */
		StartGuestLobbySearch();
		/* Linger so the guest receives + acts, and our re-announce reaches it. */
		for (int i = 0; i < 700; ++i) Tick(1);
		int ok = gOwnLobbyCreated;               /* invite-sent depends on the peer role */
		printf("[host] created=%d inviteSent=%d\n", gOwnLobbyCreated, gInviteSent);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("INVITE-HOST %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else if (isInvitee)
	{
		CreateOwnLobby("InviteTest:Guest"); /* discoverable so the host can target us */
		for (int i = 0; i < 900 && !gJoined; ++i) Tick(1);
		int joinedRoom = gJoined && strcmp(gJoinedLobbyId, gOwnLobbyId) != 0;
		int ok = gInviteReceived && gInviteAccepted && joinedRoom;
		printf("[invitee] received=%d accepted=%d joinedRoom=%d\n", gInviteReceived, gInviteAccepted, joinedRoom);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("INVITE-INVITEE %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else if (isJoiner)
	{
		/* No own lobby: AUTOJOIN discovers the host's room and joins by UiEventId. */
		for (int i = 0; i < 900 && !gJoined; ++i) Tick(1);
		int ok = gJoinAccepted && gJoined;
		printf("[joiner] joinAccepted=%d joined=%d\n", gJoinAccepted, gJoined);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("INVITE-JOINER %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}

	printf("unknown role %s\n", role);
	EOS_Platform_Release(gPlatform); EOS_Shutdown();
	return 4;
}
