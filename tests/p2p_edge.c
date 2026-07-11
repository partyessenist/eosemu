// Two-instance P2P edge-case test:
//
//   1. Queue-full backpressure: the host caps its incoming packet queue at
//      4096 bytes and drains ONE packet per tick while the joiner bursts 60
//      reliable-ordered 1000-byte packets. Every packet must still arrive
//      exactly once, in order (reliable packets defer on a full inbox, they
//      are never dropped), and the queue-full notification must fire.
//   2. ConnectionIgnored: the host binds its connection-request listener for
//      socket "CHAT" only; a joiner request on "NOLISTEN" must come back as
//      ConnectionClosed with EOS_CCR_ConnectionIgnored.
//   3. Interruption: after the exchange the host process exits without closing;
//      the joiner keeps sending on "CHAT" and must see PeerConnectionInterrupted
//      once retransmits exhaust, then ConnectionClosed(EOS_CCR_TimedOut) after
//      the reconnect grace period.
//
// The host PUID is discovered through a marker lobby, as in lan.c.

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
#include "eos_p2p.h"
#include "eos_lobby.h"

#define PACKET_COUNT 60
#define PACKET_BYTES 1000

static EOS_HPlatform gPlatform;
static EOS_HLobby gLobby;
static EOS_HP2P gP2P;
static EOS_ProductUserId gLocalPuid;

static int gConnectDone;
static int gLobbyCreated;
static int gQueueFullSeen;
static int gIgnoredSeen;      /* join: NOLISTEN closed with ConnectionIgnored */
static int gInterruptedSeen;  /* join: CHAT interrupted after host vanished */
static int gTimedOutSeen;     /* join: CHAT closed with TimedOut afterwards */
static int gDoneSeen;         /* join: host's DONE marker packet arrived */

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
	gLobbyCreated = (D->ResultCode == EOS_Success);
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

static void EOS_CALL OnQueueFull(const EOS_P2P_OnIncomingPacketQueueFullInfo* D)
{
	(void)D;
	gQueueFullSeen = 1;
}

static void EOS_CALL OnClosed(const EOS_P2P_OnRemoteConnectionClosedInfo* D)
{
	printf("  join: closed socket='%s' reason=%d\n", D->SocketId ? D->SocketId->SocketName : "?", (int)D->Reason);
	if (D->SocketId && strcmp(D->SocketId->SocketName, "NOLISTEN") == 0 && D->Reason == EOS_CCR_ConnectionIgnored)
		gIgnoredSeen = 1;
	if (D->SocketId && strcmp(D->SocketId->SocketName, "CHAT") == 0 && D->Reason == EOS_CCR_TimedOut)
		gTimedOutSeen = 1;
}

static void EOS_CALL OnInterrupted(const EOS_P2P_OnPeerConnectionInterruptedInfo* D)
{
	printf("  join: interrupted socket='%s'\n", D->SocketId ? D->SocketId->SocketName : "?");
	if (D->SocketId && strcmp(D->SocketId->SocketName, "CHAT") == 0)
		gInterruptedSeen = 1;
}

static void FillSocket(EOS_P2P_SocketId* sid, const char* name)
{
	memset(sid, 0, sizeof(*sid));
	sid->ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
	strncpy(sid->SocketName, name, sizeof(sid->SocketName) - 1);
}

static void SendOn(const char* sock, EOS_ProductUserId to, const void* data, uint32_t len)
{
	EOS_P2P_SocketId sid;
	FillSocket(&sid, sock);
	EOS_P2P_SendPacketOptions po = {0};
	po.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	po.LocalUserId = gLocalPuid;
	po.RemoteUserId = to;
	po.SocketId = &sid;
	po.Channel = 0;
	po.DataLengthBytes = len;
	po.Data = data;
	po.bAllowDelayedDelivery = EOS_TRUE;
	po.Reliability = EOS_PR_ReliableOrdered;
	EOS_P2P_SendPacket(gP2P, &po);
}

/* Receives at most one packet; returns bytes read, 0 if none. */
static uint32_t RecvOne(char* buf, uint32_t cap, EOS_ProductUserId* from, EOS_P2P_SocketId* sid)
{
	EOS_P2P_GetNextReceivedPacketSizeOptions so = {0};
	so.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
	so.LocalUserId = gLocalPuid;
	uint32_t size = 0;
	if (EOS_P2P_GetNextReceivedPacketSize(gP2P, &so, &size) != EOS_Success || size == 0)
		return 0;
	uint8_t ch = 0; uint32_t got = 0;
	EOS_P2P_ReceivePacketOptions ro = {0};
	ro.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
	ro.LocalUserId = gLocalPuid;
	ro.MaxDataSizeBytes = cap;
	if (EOS_P2P_ReceivePacket(gP2P, &ro, from, sid, &ch, buf, &got) != EOS_Success)
		return 0;
	return got;
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "host";
	int isHost = strcmp(role, "host") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuP2PEdge"; io.ProductVersion = "1.0";
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
	gP2P = EOS_Platform_GetP2PInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);

	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);
	printf("[%s] connected\n", role);

	if (isHost)
	{
		/* Listener bound for CHAT only -- NOLISTEN requests must be ignored. */
		EOS_P2P_SocketId chatSid;
		FillSocket(&chatSid, "CHAT");
		EOS_P2P_AddNotifyPeerConnectionRequestOptions no = {0};
		no.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
		no.LocalUserId = gLocalPuid;
		no.SocketId = &chatSid;
		EOS_P2P_AddNotifyPeerConnectionRequest(gP2P, &no, NULL, &OnConnReq);

		EOS_P2P_AddNotifyIncomingPacketQueueFullOptions qo = {0};
		qo.ApiVersion = EOS_P2P_ADDNOTIFYINCOMINGPACKETQUEUEFULL_API_LATEST;
		EOS_P2P_AddNotifyIncomingPacketQueueFull(gP2P, &qo, NULL, &OnQueueFull);

		/* Cap the incoming queue at ~4 packets to force backpressure. */
		EOS_P2P_SetPacketQueueSizeOptions qs = {0};
		qs.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
		qs.IncomingPacketQueueMaxSizeBytes = 4096;
		qs.OutgoingPacketQueueMaxSizeBytes = EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED;
		EOS_P2P_SetPacketQueueSize(gP2P, &qs);

		/* Marker lobby so the joiner can learn our PUID. */
		EOS_Lobby_CreateLobbyOptions lo; memset(&lo, 0, sizeof(lo));
		lo.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
		lo.LocalUserId = gLocalPuid;
		lo.MaxLobbyMembers = 2;
		lo.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
		lo.bAllowInvites = EOS_TRUE;
		lo.BucketId = "P2PEdge:Test";
		EOS_Lobby_CreateLobby(gLobby, &lo, NULL, &OnCreateLobby);

		/* Drain at most ONE packet per tick; expect PACKET_COUNT in order. */
		int nextIdx = 0, misordered = 0;
		EOS_ProductUserId sender = NULL;
		char buf[PACKET_BYTES + 8];
		for (int i = 0; i < 3000 && nextIdx < PACKET_COUNT; ++i)
		{
			Tick(1);
			EOS_ProductUserId from = NULL; EOS_P2P_SocketId sid = {0};
			uint32_t got = RecvOne(buf, sizeof(buf), &from, &sid);
			if (got == 0) continue;
			if (strcmp(sid.SocketName, "CHAT") != 0) continue;
			int idx;
			memcpy(&idx, buf, 4);
			if (got != PACKET_BYTES || idx != nextIdx) { misordered = 1; printf("  host: bad packet idx=%d want=%d len=%u\n", idx, nextIdx, got); break; }
			++nextIdx;
			sender = from;
		}
		printf("  host: received %d/%d in order, queueFull=%d\n", nextIdx, PACKET_COUNT, gQueueFullSeen);

		int ok = (nextIdx == PACKET_COUNT) && !misordered && gQueueFullSeen && gLobbyCreated;
		if (ok && sender)
		{
			SendOn("CHAT", sender, "DONE", 5);
			Tick(100); /* let the marker and its ack settle before vanishing */
		}
		printf("[host] ok=%d\n", ok);
		/* Exit WITHOUT closing the connection -- the joiner must detect the
		 * vanished peer via retransmit exhaustion. Platform_Release stops the
		 * transport but sends no P2P close. */
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("EDGE-HOST %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else
	{
		EOS_P2P_AddNotifyPeerConnectionRequestOptions no = {0};
		no.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
		no.LocalUserId = gLocalPuid;
		EOS_P2P_AddNotifyPeerConnectionRequest(gP2P, &no, NULL, &OnConnReq);

		EOS_P2P_AddNotifyPeerConnectionClosedOptions co = {0};
		co.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
		co.LocalUserId = gLocalPuid;
		EOS_P2P_AddNotifyPeerConnectionClosed(gP2P, &co, NULL, &OnClosed);

		EOS_P2P_AddNotifyPeerConnectionInterruptedOptions ino = {0};
		ino.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST;
		ino.LocalUserId = gLocalPuid;
		EOS_P2P_AddNotifyPeerConnectionInterrupted(gP2P, &ino, NULL, &OnInterrupted);

		/* Find the host through its marker lobby. */
		for (int i = 0; i < 150; ++i) Tick(1);
		EOS_ProductUserId hostPuid = NULL;
		{
			EOS_HLobbySearch search = NULL;
			EOS_Lobby_CreateLobbySearchOptions cso = {0};
			cso.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
			cso.MaxResults = 10;
			if (EOS_Lobby_CreateLobbySearch(gLobby, &cso, &search) == EOS_Success)
			{
				EOS_LobbySearch_SetParameterOptions sp = {0};
				sp.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
				EOS_Lobby_AttributeData ad = {0};
				ad.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
				ad.Key = EOS_LOBBY_SEARCH_BUCKET_ID; ad.ValueType = EOS_AT_STRING; ad.Value.AsUtf8 = "P2PEdge:Test";
				sp.Parameter = &ad; sp.ComparisonOp = EOS_CO_EQUAL;
				EOS_LobbySearch_SetParameter(search, &sp);
				EOS_LobbySearch_FindOptions fo = {0};
				fo.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
				fo.LocalUserId = gLocalPuid;
				/* Synchronous-ish: fire and poll the result count. */
				EOS_LobbySearch_Find(search, &fo, NULL, NULL);
				for (int i = 0; i < 250 && !hostPuid; ++i)
				{
					Tick(1);
					EOS_LobbySearch_GetSearchResultCountOptions gco = {0};
					gco.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
					if (EOS_LobbySearch_GetSearchResultCount(search, &gco) > 0)
					{
						EOS_LobbySearch_CopySearchResultByIndexOptions cp = {0};
						cp.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
						cp.LobbyIndex = 0;
						EOS_HLobbyDetails details = NULL;
						if (EOS_LobbySearch_CopySearchResultByIndex(search, &cp, &details) == EOS_Success && details)
						{
							EOS_LobbyDetails_GetLobbyOwnerOptions go = {0};
							go.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
							hostPuid = EOS_LobbyDetails_GetLobbyOwner(details, &go);
							EOS_LobbyDetails_Release(details);
						}
					}
				}
				EOS_LobbySearch_Release(search);
			}
		}
		if (!hostPuid)
		{
			printf("[join] host not found\nEDGE-JOIN FAIL\n");
			EOS_Platform_Release(gPlatform); EOS_Shutdown();
			return 3;
		}

		/* Phase 1: request on a socket the host has no listener for. */
		SendOn("NOLISTEN", hostPuid, "x", 1);
		for (int i = 0; i < 250 && !gIgnoredSeen; ++i) Tick(1);
		printf("  join: ignored=%d\n", gIgnoredSeen);

		/* Phase 2: burst the payload the host drains slowly. */
		char pkt[PACKET_BYTES];
		memset(pkt, 0xAB, sizeof(pkt));
		for (int idx = 0; idx < PACKET_COUNT; ++idx)
		{
			memcpy(pkt, &idx, 4);
			SendOn("CHAT", hostPuid, pkt, PACKET_BYTES);
		}
		/* Wait for the host's DONE marker (it arrives once all 60 landed). */
		char buf[PACKET_BYTES + 8];
		for (int i = 0; i < 3000 && !gDoneSeen; ++i)
		{
			Tick(1);
			EOS_ProductUserId from = NULL; EOS_P2P_SocketId sid = {0};
			uint32_t got = RecvOne(buf, sizeof(buf), &from, &sid);
			if (got == 5 && strcmp(sid.SocketName, "CHAT") == 0 && memcmp(buf, "DONE", 5) == 0)
				gDoneSeen = 1;
		}
		printf("  join: done=%d\n", gDoneSeen);

		/* Phase 3: the host exits ~2s after DONE. Keep sending; expect
		 * interrupted, then closed(TimedOut). */
		if (gDoneSeen)
		{
			Tick(200); /* ~4s: let the host vanish */
			for (int i = 0; i < 1200 && !gTimedOutSeen; ++i)
			{
				if ((i % 25) == 0 && !gInterruptedSeen)
				{
					SendOn("CHAT", hostPuid, "ping", 5);
				}
				Tick(1);
			}
		}
		printf("  join: interrupted=%d timedOut=%d\n", gInterruptedSeen, gTimedOutSeen);

		int ok = gIgnoredSeen && gDoneSeen && gInterruptedSeen && gTimedOutSeen;
		printf("[join] ignored=%d done=%d interrupted=%d timedout=%d\n", gIgnoredSeen, gDoneSeen, gInterruptedSeen, gTimedOutSeen);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("EDGE-JOIN %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
}
