// Reliable/ordered P2P acceptance test (two processes on one machine).
//
// The joiner sends N packets as EOS_PR_ReliableOrdered, each carrying its index.
// The host verifies it receives EXACTLY N packets, each exactly once, strictly
// in order (0,1,2,...,N-1). Run under EOSEMU_P2P_TESTLOSS (per-mille packet drop
// on the data+ack path) so the seq/ack/retransmit + reorder layer is actually
// exercised -- with the old UDP-passthrough behaviour this test fails.
//
//   host:  create a lobby (so the joiner can discover the host's PUID), then
//          receive + verify the ordered stream.
//   join:  find the host via lobby owner, blast N ReliableOrdered packets, then
//          keep ticking so retransmissions converge.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
#include "eos_p2p.h"
#include "eos_lobby.h"

#define N_PACKETS 120
static const char SOCKET_NAME[] = "REL";

static EOS_HPlatform gPlatform;
static EOS_HP2P gP2P;
static EOS_HLobby gLobby;
static EOS_ProductUserId gLocalPuid;
static int gConnectDone;

// host verification state
static int gRecv = 0;       // total packets received
static int gExpected = 0;   // next index expected in order
static int gOrderOk = 1;    // cleared on any gap/dup/out-of-order

// joiner state
static EOS_ProductUserId gOwner;
static int gSent = 0;
static EOS_HLobbySearch gSearch;

static void EOS_CALL LogCb(const EOS_LogMessage* M)
{
	if (M->Level <= EOS_LOG_Warning)
		printf("  [%s] %s\n", M->Category ? M->Category : "?", M->Message ? M->Message : "");
}

static void Tick(int frames)
{
	for (int i = 0; i < frames; ++i) { EOS_Platform_Tick(gPlatform); SLEEP_MS(15); }
}

static void EOS_CALL OnConnect(const EOS_Connect_LoginCallbackInfo* D)
{
	gLocalPuid = D->LocalUserId;
	gConnectDone = 1;
}

static void EOS_CALL OnConnReq(const EOS_P2P_OnIncomingConnectionRequestInfo* D)
{
	EOS_P2P_AcceptConnectionOptions ao = {0};
	ao.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
	ao.LocalUserId = gLocalPuid;
	ao.RemoteUserId = D->RemoteUserId;
	ao.SocketId = D->SocketId;
	EOS_P2P_AcceptConnection(gP2P, &ao);
}

static void EOS_CALL OnCreateLobby(const EOS_Lobby_CreateLobbyCallbackInfo* D)
{
	if (D->ResultCode == EOS_Success) printf("  host: lobby up\n");
}

static void EOS_CALL OnFind(const EOS_LobbySearch_FindCallbackInfo* D)
{
	if (D->ResultCode != EOS_Success) return;
	EOS_LobbySearch_GetSearchResultCountOptions co = {0};
	co.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
	if (EOS_LobbySearch_GetSearchResultCount(gSearch, &co) == 0) return;
	EOS_LobbySearch_CopySearchResultByIndexOptions cp = {0};
	cp.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
	cp.LobbyIndex = 0;
	EOS_HLobbyDetails details = NULL;
	if (EOS_LobbySearch_CopySearchResultByIndex(gSearch, &cp, &details) == EOS_Success && details)
	{
		EOS_LobbyDetails_GetLobbyOwnerOptions go = {0};
		go.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
		gOwner = EOS_LobbyDetails_GetLobbyOwner(details, &go);
		EOS_LobbyDetails_Release(details);
	}
}

static void SendSeq(EOS_ProductUserId to, uint32_t idx)
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
	po.DataLengthBytes = sizeof(idx);
	po.Data = &idx;
	po.bAllowDelayedDelivery = EOS_TRUE;
	po.Reliability = EOS_PR_ReliableOrdered;
	EOS_P2P_SendPacket(gP2P, &po);
}

static void DrainVerify(void)
{
	for (;;)
	{
		EOS_P2P_GetNextReceivedPacketSizeOptions so = {0};
		so.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
		so.LocalUserId = gLocalPuid;
		uint32_t size = 0;
		if (!(EOS_P2P_GetNextReceivedPacketSize(gP2P, &so, &size) == EOS_Success && size > 0)) break;

		unsigned char buf[64]; EOS_ProductUserId peer = NULL; EOS_P2P_SocketId sid = {0}; uint8_t ch = 0; uint32_t got = 0;
		EOS_P2P_ReceivePacketOptions ro = {0};
		ro.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
		ro.LocalUserId = gLocalPuid;
		ro.MaxDataSizeBytes = sizeof(buf);
		if (EOS_P2P_ReceivePacket(gP2P, &ro, &peer, &sid, &ch, buf, &got) != EOS_Success) break;
		if (got >= 4)
		{
			uint32_t idx; memcpy(&idx, buf, 4);
			if ((int)idx == gExpected) gExpected++;
			else gOrderOk = 0; // gap, duplicate, or out-of-order
			gRecv++;
		}
	}
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "host";
	int isHost = strcmp(role, "host") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuRel"; io.ProductVersion = "1.0";
	if (EOS_Initialize(&io) != EOS_Success) { printf("init failed\n"); return 1; }
	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Warning);

	EOS_Platform_Options po; memset(&po, 0, sizeof(po));
	po.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	po.ProductId = "prod"; po.SandboxId = "sand"; po.DeploymentId = "deploy";
	po.ClientCredentials.ClientId = "cid"; po.ClientCredentials.ClientSecret = "secret";
	gPlatform = EOS_Platform_Create(&po);
	if (!gPlatform) { printf("platform create failed\n"); EOS_Shutdown(); return 2; }

	gP2P = EOS_Platform_GetP2PInterface(gPlatform);
	gLobby = EOS_Platform_GetLobbyInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);

	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);
	printf("[%s] connected\n", role);

	EOS_P2P_AddNotifyPeerConnectionRequestOptions no = {0};
	no.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
	no.LocalUserId = gLocalPuid;
	EOS_P2P_AddNotifyPeerConnectionRequest(gP2P, &no, NULL, &OnConnReq);

	if (isHost)
	{
		EOS_Lobby_CreateLobbyOptions lo; memset(&lo, 0, sizeof(lo));
		lo.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
		lo.LocalUserId = gLocalPuid;
		lo.MaxLobbyMembers = 4;
		lo.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
		lo.bAllowInvites = EOS_TRUE;
		lo.BucketId = "RelTest";
		EOS_Lobby_CreateLobby(gLobby, &lo, NULL, &OnCreateLobby);

		// Receive + verify for up to ~18s, finishing early once all N arrive.
		for (int i = 0; i < 1200 && gRecv < N_PACKETS; ++i) { Tick(1); DrainVerify(); }
		DrainVerify();

		int ok = (gRecv == N_PACKETS) && gOrderOk && (gExpected == N_PACKETS);
		printf("[host] received=%d expected=%d orderOk=%d (want %d, in order)\n", gRecv, gExpected, gOrderOk, N_PACKETS);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("REL-HOST %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else
	{
		for (int i = 0; i < 150; ++i) Tick(1); // let discovery see the host

		EOS_Lobby_CreateLobbySearchOptions cso = {0};
		cso.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
		cso.MaxResults = 10;
		if (EOS_Lobby_CreateLobbySearch(gLobby, &cso, &gSearch) == EOS_Success)
		{
			EOS_LobbySearch_SetParameterOptions sp = {0};
			sp.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
			EOS_Lobby_AttributeData ad = {0};
			ad.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
			ad.Key = EOS_LOBBY_SEARCH_BUCKET_ID; ad.ValueType = EOS_AT_STRING; ad.Value.AsUtf8 = "RelTest";
			sp.Parameter = &ad; sp.ComparisonOp = EOS_CO_EQUAL;
			EOS_LobbySearch_SetParameter(gSearch, &sp);
			EOS_LobbySearch_FindOptions fo = {0};
			fo.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
			fo.LocalUserId = gLocalPuid;
			EOS_LobbySearch_Find(gSearch, &fo, NULL, &OnFind);
		}
		for (int i = 0; i < 200 && !gOwner; ++i) Tick(1);

		if (gOwner)
		{
			// Blast all N ordered packets; the induced loss will drop ~30%, which
			// the retransmit pump (driven by Tick) must recover.
			for (uint32_t i = 0; i < N_PACKETS; ++i)
			{
				SendSeq(gOwner, i);
				gSent++;
				if ((i % 16) == 15) Tick(1); // let the pump start retransmitting
			}
			// Keep ticking so retransmissions + acks converge.
			for (int i = 0; i < 500; ++i) Tick(1);
		}

		int ok = (gOwner != NULL) && (gSent == N_PACKETS);
		printf("[join] owner=%s sent=%d\n", gOwner ? "yes" : "no", gSent);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("REL-JOIN %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
}
