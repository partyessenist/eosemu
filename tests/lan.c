// Two-instance LAN acceptance test.
//
// Run as two processes on the same machine (distinct EOSEMU_PROFILE):
//   host:  exercises CreateLobby, then answers a P2P "CHAT" ping, then creates
//          a session and advertises it.
//   join:  discovers the host, searches+joins the lobby, exchanges a P2P
//          packet, and searches for the session.
//
// Exits 0 only if the role's checks all pass. Drives everything from a Tick
// loop, exactly as a game would.

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
#include "eos_auth.h"
#include "eos_connect.h"
#include "eos_logging.h"
#include "eos_p2p.h"
#include "eos_lobby.h"
#include "eos_sessions.h"

static EOS_HPlatform gPlatform;
static EOS_HLobby gLobby;
static EOS_HSessions gSessions;
static EOS_HP2P gP2P;
static EOS_ProductUserId gLocalPuid;

static int gConnectDone;
static char gLobbyId[256];
static int gLobbyCreated;
static int gLobbyJoined;
static char gJoinedLobbyId[256];
static int gSearchFound;
static int gP2PGot;
static EOS_ProductUserId gPeerFrom; /* who the last P2P packet came from */
static int gSessionFound;
static int gMemberAttrSet;    /* join: UpdateLobby(member attr) completed OK */
static int gMemberUpdateSeen; /* host: member update arrived with ready=true */

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
		if (D->LobbyId) strncpy(gJoinedLobbyId, D->LobbyId, sizeof(gJoinedLobbyId) - 1);
		printf("  join: joined lobby %s\n", D->LobbyId ? D->LobbyId : "?");
	}
}

static void EOS_CALL OnUpdateLobby(const EOS_Lobby_UpdateLobbyCallbackInfo* D)
{
	if (D->ResultCode == EOS_Success) gMemberAttrSet = 1;
	else printf("  join: UpdateLobby failed (%d)\n", (int)D->ResultCode);
}

static EOS_HLobbySearch gSearch;
static void EOS_CALL OnFind(const EOS_LobbySearch_FindCallbackInfo* D)
{
	if (D->ResultCode != EOS_Success) return;
	EOS_LobbySearch_GetSearchResultCountOptions co = {0};
	co.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
	uint32_t n = EOS_LobbySearch_GetSearchResultCount(gSearch, &co);
	printf("  join: lobby search returned %u result(s)\n", n);
	if (n > 0)
	{
		gSearchFound = 1;
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

static EOS_HSessionSearch gSessSearch;
static void EOS_CALL OnSessFind(const EOS_SessionSearch_FindCallbackInfo* D)
{
	if (D->ResultCode != EOS_Success) return;
	EOS_SessionSearch_GetSearchResultCountOptions co = {0};
	co.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
	uint32_t n = EOS_SessionSearch_GetSearchResultCount(gSessSearch, &co);
	printf("  join: session search returned %u result(s)\n", n);
	if (n > 0) gSessionFound = 1;
}

static const char SOCKET_NAME[] = "CHAT";

static EOS_HLobby gLobbyForNotify;
static EOS_ProductUserId gLocalForNotify;
static void EOS_CALL OnMemberUpdate(const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* D)
{
	/* The joiner set ready=true on itself; read it back through the details
	 * handle to prove the value (not just a version bump) replicated. */
	EOS_Lobby_CopyLobbyDetailsHandleOptions ch = {0};
	ch.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
	ch.LobbyId = D->LobbyId;
	ch.LocalUserId = gLocalForNotify;
	EOS_HLobbyDetails det = NULL;
	if (EOS_Lobby_CopyLobbyDetailsHandle(gLobbyForNotify, &ch, &det) == EOS_Success && det)
	{
		EOS_LobbyDetails_CopyMemberAttributeByKeyOptions mk = {0};
		mk.ApiVersion = EOS_LOBBYDETAILS_COPYMEMBERATTRIBUTEBYKEY_API_LATEST;
		mk.TargetUserId = D->TargetUserId;
		mk.AttrKey = "ready";
		EOS_Lobby_Attribute* attr = NULL;
		if (EOS_LobbyDetails_CopyMemberAttributeByKey(det, &mk, &attr) == EOS_Success && attr)
		{
			if (attr->Data && attr->Data->ValueType == EOS_AT_BOOLEAN && attr->Data->Value.AsBool == EOS_TRUE)
				gMemberUpdateSeen = 1;
			EOS_Lobby_Attribute_Release(attr);
		}
		EOS_LobbyDetails_Release(det);
	}
	printf("  host: member update received (ready=%d)\n", gMemberUpdateSeen);
}

static void EOS_CALL OnConnReq(const EOS_P2P_OnIncomingConnectionRequestInfo* D)
{
	EOS_P2P_AcceptConnectionOptions ao = {0};
	ao.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
	ao.LocalUserId = gLocalPuid;
	ao.RemoteUserId = D->RemoteUserId;
	ao.SocketId = D->SocketId;
	EOS_P2P_AcceptConnection(gP2P, &ao);
	printf("  p2p: accepted connection\n");
}

static void PumpP2P(void)
{
	EOS_P2P_GetNextReceivedPacketSizeOptions so = {0};
	so.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
	so.LocalUserId = gLocalPuid;
	uint32_t size = 0;
	if (EOS_P2P_GetNextReceivedPacketSize(gP2P, &so, &size) == EOS_Success && size > 0)
	{
		char buf[1200]; EOS_ProductUserId peer = NULL; EOS_P2P_SocketId sid = {0}; uint8_t ch = 0; uint32_t got = 0;
		EOS_P2P_ReceivePacketOptions ro = {0};
		ro.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
		ro.LocalUserId = gLocalPuid;
		ro.MaxDataSizeBytes = sizeof(buf);
		if (EOS_P2P_ReceivePacket(gP2P, &ro, &peer, &sid, &ch, buf, &got) == EOS_Success)
		{
			buf[got < sizeof(buf) ? got : sizeof(buf) - 1] = 0;
			if (!gP2PGot) printf("  p2p: received '%s' on socket '%s'\n", buf, sid.SocketName);
			gP2PGot = 1;
			gPeerFrom = peer;
		}
	}
}

static void SendP2P(EOS_ProductUserId to, const char* msg)
{
	EOS_P2P_SocketId sid = {0};
	sid.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
	strncpy(sid.SocketName, SOCKET_NAME, sizeof(sid.SocketName) - 1);
	EOS_P2P_SendPacketOptions po = {0};
	po.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	po.LocalUserId = gLocalPuid;
	po.RemoteUserId = to;
	po.SocketId = &sid;
	po.Channel = 0;
	po.DataLengthBytes = (uint32_t)strlen(msg) + 1;
	po.Data = msg;
	po.bAllowDelayedDelivery = EOS_TRUE;
	po.Reliability = EOS_PR_ReliableOrdered;
	EOS_P2P_SendPacket(gP2P, &po);
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "host";
	int isHost = strcmp(role, "host") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuLan"; io.ProductVersion = "1.0";
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
	gSessions = EOS_Platform_GetSessionsInterface(gPlatform);
	gP2P = EOS_Platform_GetP2PInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);

	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);
	printf("[%s] connected, puid=%p\n", role, (void*)gLocalPuid);

	// Bind a P2P connection-request listener on both sides.
	EOS_P2P_AddNotifyPeerConnectionRequestOptions no = {0};
	no.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
	no.LocalUserId = gLocalPuid;
	EOS_P2P_AddNotifyPeerConnectionRequest(gP2P, &no, NULL, &OnConnReq);

	gLobbyForNotify = gLobby;
	gLocalForNotify = gLocalPuid;

	if (isHost)
	{
		EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions muo = {0};
		muo.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERUPDATERECEIVED_API_LATEST;
		EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(gLobby, &muo, NULL, &OnMemberUpdate);

		EOS_Lobby_CreateLobbyOptions lo; memset(&lo, 0, sizeof(lo));
		lo.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
		lo.LocalUserId = gLocalPuid;
		lo.MaxLobbyMembers = 4;
		lo.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
		lo.bAllowInvites = EOS_TRUE;
		lo.BucketId = "GameMode:Region";
		EOS_Lobby_CreateLobby(gLobby, &lo, NULL, &OnCreateLobby);

		// Create + advertise a session.
		EOS_Sessions_CreateSessionModificationOptions smo; memset(&smo, 0, sizeof(smo));
		smo.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
		smo.SessionName = "MySession"; smo.BucketId = "GameMode:Region"; smo.MaxPlayers = 4;
		smo.LocalUserId = gLocalPuid;
		EOS_HSessionModification smod = NULL;
		if (EOS_Sessions_CreateSessionModification(gSessions, &smo, &smod) == EOS_Success)
		{
			EOS_Sessions_UpdateSessionOptions uso = {0};
			uso.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
			uso.SessionModificationHandle = smod;
			EOS_Sessions_UpdateSession(gSessions, &uso, NULL, NULL);
			EOS_SessionModification_Release(smod);
		}

		// Run: announce, answer P2P, for ~8 seconds. Reply to the actual
		// sender (gPeerFrom), not our own PUID.
		int replied = 0;
		for (int i = 0; i < 400; ++i)
		{
			Tick(1); PumpP2P();
			if (gP2PGot && gPeerFrom && !replied) { SendP2P(gPeerFrom, "hello from host"); replied = 1; }
		}
		printf("[host] created=%d p2p=%d mattr=%d\n", gLobbyCreated, gP2PGot, gMemberUpdateSeen);
		int ok = gLobbyCreated && gP2PGot && gMemberUpdateSeen;
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("LAN-HOST %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else
	{
		// Give discovery time to see the host.
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
			ad.Key = EOS_LOBBY_SEARCH_BUCKET_ID; ad.ValueType = EOS_AT_STRING; ad.Value.AsUtf8 = "GameMode:Region";
			sp.Parameter = &ad; sp.ComparisonOp = EOS_CO_EQUAL;
			EOS_LobbySearch_SetParameter(gSearch, &sp);

			EOS_LobbySearch_FindOptions fo = {0};
			fo.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
			fo.LocalUserId = gLocalPuid;
			EOS_LobbySearch_Find(gSearch, &fo, NULL, &OnFind);
		}
		for (int i = 0; i < 150 && !gLobbyJoined; ++i) Tick(1);

		// Member attribute: set ready=true on ourselves. The owner must merge
		// it, re-announce, and fire its MemberUpdateReceived (asserted host-side).
		if (gLobbyJoined && gJoinedLobbyId[0])
		{
			EOS_Lobby_UpdateLobbyModificationOptions um = {0};
			um.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
			um.LobbyId = gJoinedLobbyId;
			um.LocalUserId = gLocalPuid;
			EOS_HLobbyModification lmod = NULL;
			if (EOS_Lobby_UpdateLobbyModification(gLobby, &um, &lmod) == EOS_Success)
			{
				EOS_LobbyModification_AddMemberAttributeOptions ma = {0};
				ma.ApiVersion = EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST;
				EOS_Lobby_AttributeData rd = {0};
				rd.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
				rd.Key = "ready"; rd.ValueType = EOS_AT_BOOLEAN; rd.Value.AsBool = EOS_TRUE;
				ma.Attribute = &rd; ma.Visibility = EOS_LAT_PUBLIC;
				EOS_LobbyModification_AddMemberAttribute(lmod, &ma);
				EOS_Lobby_UpdateLobbyOptions ul = {0};
				ul.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
				ul.LobbyModificationHandle = lmod;
				EOS_Lobby_UpdateLobby(gLobby, &ul, NULL, &OnUpdateLobby);
				EOS_LobbyModification_Release(lmod);
			}
			for (int i = 0; i < 50 && !gMemberAttrSet; ++i) Tick(1);
		}

		// Session search.
		EOS_Sessions_CreateSessionSearchOptions scso = {0};
		scso.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
		scso.MaxSearchResults = 10;
		if (EOS_Sessions_CreateSessionSearch(gSessions, &scso, &gSessSearch) == EOS_Success)
		{
			EOS_SessionSearch_SetParameterOptions ssp = {0};
			ssp.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
			EOS_Sessions_AttributeData sad = {0};
			sad.ApiVersion = EOS_SESSIONS_SESSIONATTRIBUTEDATA_API_LATEST;
			sad.Key = EOS_SESSIONS_SEARCH_BUCKET_ID; sad.ValueType = EOS_AT_STRING; sad.Value.AsUtf8 = "GameMode:Region";
			ssp.Parameter = &sad; ssp.ComparisonOp = EOS_CO_EQUAL;
			EOS_SessionSearch_SetParameter(gSessSearch, &ssp);
			EOS_SessionSearch_FindOptions sfo = {0};
			sfo.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
			sfo.LocalUserId = gLocalPuid;
			EOS_SessionSearch_Find(gSessSearch, &sfo, NULL, &OnSessFind);
		}
		for (int i = 0; i < 100 && !gSessionFound; ++i) Tick(1);

		// P2P: send to the lobby owner if we found one, else broadcast to peers.
		// We need the host's product id; recover it from the lobby member list.
		// Simplest: send to every discovered peer via friends list.
		EOS_HFriends friends = EOS_Platform_GetFriendsInterface(gPlatform);
		(void)friends;
		// Send to host by copying lobby owner from the joined lobby.
		{
			EOS_Lobby_CopyLobbyDetailsHandleOptions ch = {0};
			ch.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
			ch.LobbyId = gLobbyId[0] ? gLobbyId : NULL;
			ch.LocalUserId = gLocalPuid;
			// gLobbyId only set on host; joiner uses search result's owner instead.
		}
		// Use the search result's owner for the P2P target.
		if (gSearchFound && gSearch)
		{
			EOS_LobbySearch_CopySearchResultByIndexOptions cp = {0};
			cp.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
			cp.LobbyIndex = 0;
			EOS_HLobbyDetails details = NULL;
			if (EOS_LobbySearch_CopySearchResultByIndex(gSearch, &cp, &details) == EOS_Success && details)
			{
				EOS_LobbyDetails_GetLobbyOwnerOptions go = {0};
				go.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
				EOS_ProductUserId owner = EOS_LobbyDetails_GetLobbyOwner(details, &go);
				if (owner) { for (int k = 0; k < 20; ++k) { SendP2P(owner, "hello from joiner"); Tick(2); } }
				EOS_LobbyDetails_Release(details);
			}
		}
		for (int i = 0; i < 100; ++i) { Tick(1); PumpP2P(); }

		int ok = gSearchFound && gLobbyJoined && gSessionFound && gMemberAttrSet && gP2PGot;
		printf("[join] found=%d joined=%d session=%d mattr=%d p2p=%d\n", gSearchFound, gLobbyJoined, gSessionFound, gMemberAttrSet, gP2PGot);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("LAN-JOIN %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
}
