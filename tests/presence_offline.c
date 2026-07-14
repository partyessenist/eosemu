// Peer liveness (offline / back-online) test.
//
// Reproduces the reported bug: a peer that closes its game must stop showing as
// online, while staying in the friends list, and must come back online if it
// returns. Run as two roles on one machine (distinct EOSEMU_PROFILE):
//
//   goer:    brings up a platform, announces for a few seconds, then cleanly
//            releases (which broadcasts a Goodbye) and exits. The runner starts
//            it TWICE with the SAME profile, so the second run is the same Epic
//            account id returning to the LAN.
//   watcher: discovers the goer as a friend and asserts the full sequence
//              Online -> Offline -> Online
//            via EOS_Presence_CopyPresence, that an OnPresenceChanged fired for
//            the offline transition, and that the peer stays a friend the whole
//            time (GetFriendsCount >= 1 and GetStatus == EOS_FS_Friends).
//
// Exits 0 only if the watcher observed the whole sequence.

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	define SLEEP_MS(ms) Sleep(ms)
#	define NOW_MS() GetTickCount64()
#else
#	include <time.h>
#	include <sys/time.h>
static void SLEEP_MS(int ms) { struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L}; nanosleep(&ts, NULL); }
static unsigned long long NOW_MS(void) { struct timeval tv; gettimeofday(&tv, NULL); return (unsigned long long)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL; }
#endif

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_logging.h"
#include "eos_friends.h"
#include "eos_presence.h"

#define PRODUCT_ID "prod"

static EOS_HPlatform gPlatform;
static EOS_HPresence gPresence;
static EOS_HFriends gFriends;
static int gConnectDone;
static EOS_EpicAccountId gPeer;   // the goer, once discovered
static int gPeerNotify;           // OnPresenceChanged count for gPeer

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

static void EOS_CALL OnConnect(const EOS_Connect_LoginCallbackInfo* D) { (void)D; gConnectDone = 1; }
static void EOS_CALL OnPresenceChanged(const EOS_Presence_PresenceChangedCallbackInfo* D)
{
	// Count only notifications about the peer we are tracking, so a transition
	// can be tied to its own callback rather than to any presence churn.
	if (gPeer != NULL && D->PresenceUserId == gPeer) ++gPeerNotify;
}

// The peer's replicated presence status, or -1 when unavailable.
static int PeerStatus(EOS_EpicAccountId peer)
{
	EOS_Presence_CopyPresenceOptions cpo = {0};
	cpo.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
	cpo.TargetUserId = peer;
	EOS_Presence_Info* info = NULL;
	int st = -1;
	if (EOS_Presence_CopyPresence(gPresence, &cpo, &info) == EOS_Success && info)
	{
		st = (int)info->Status;
		EOS_Presence_Info_Release(info);
	}
	return st;
}

static int PeerIsFriend(EOS_EpicAccountId peer)
{
	EOS_Friends_GetFriendsCountOptions fco = {0};
	fco.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
	if (EOS_Friends_GetFriendsCount(gFriends, &fco) < 1) return 0;
	EOS_Friends_GetStatusOptions gso = {0};
	gso.ApiVersion = EOS_FRIENDS_GETSTATUS_API_LATEST;
	gso.LocalUserId = NULL;
	gso.TargetUserId = peer;
	return EOS_Friends_GetStatus(gFriends, &gso) == EOS_FS_Friends;
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "goer";
	int isGoer = strcmp(role, "goer") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuLiveness"; io.ProductVersion = "1.0";
	if (EOS_Initialize(&io) != EOS_Success) { printf("init failed\n"); return 1; }
	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Warning);

	EOS_Platform_Options po; memset(&po, 0, sizeof(po));
	po.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	po.ProductId = PRODUCT_ID; po.SandboxId = "sand"; po.DeploymentId = "deploy";
	po.ClientCredentials.ClientId = "cid"; po.ClientCredentials.ClientSecret = "secret";
	gPlatform = EOS_Platform_Create(&po);
	if (!gPlatform) { printf("platform create failed\n"); EOS_Shutdown(); return 2; }

	gPresence = EOS_Platform_GetPresenceInterface(gPlatform);
	gFriends = EOS_Platform_GetFriendsInterface(gPlatform);
	EOS_HConnect connect = EOS_Platform_GetConnectInterface(gPlatform);

	EOS_Connect_Credentials cc; memset(&cc, 0, sizeof(cc));
	cc.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST; cc.Token = "t"; cc.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions cl; memset(&cl, 0, sizeof(cl));
	cl.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST; cl.Credentials = &cc;
	EOS_Connect_Login(connect, &cl, NULL, &OnConnect);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Tick(1);
	printf("[%s] connected\n", role);

	if (isGoer)
	{
		// Announce (default presence status is Online) for ~5s, then release
		// cleanly -- that broadcasts a Goodbye so the watcher flips us offline at
		// once instead of waiting out its liveness timeout.
		unsigned long long end = NOW_MS() + 5000;
		while (NOW_MS() < end) Tick(1);
		printf("[goer] leaving\n");
		EOS_Platform_Release(gPlatform);
		EOS_Shutdown();
		printf("GOER DONE\n");
		return 0;
	}

	EOS_Presence_AddNotifyOnPresenceChangedOptions no = {0};
	no.ApiVersion = EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST;
	EOS_Presence_AddNotifyOnPresenceChanged(gPresence, &no, NULL, &OnPresenceChanged);

	// Discover the goer as a friend.
	EOS_EpicAccountId peer = NULL;
	for (int i = 0; i < 800 && !peer; ++i)
	{
		Tick(1);
		EOS_Friends_GetFriendsCountOptions fco = {0};
		fco.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
		if (EOS_Friends_GetFriendsCount(gFriends, &fco) > 0)
		{
			EOS_Friends_GetFriendAtIndexOptions fio = {0};
			fio.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
			fio.Index = 0;
			peer = EOS_Friends_GetFriendAtIndex(gFriends, &fio);
		}
	}
	if (!peer) { printf("LIVENESS-WATCH FAIL (no peer discovered)\n"); EOS_Platform_Release(gPlatform); EOS_Shutdown(); return 3; }
	gPeer = peer;
	printf("[watcher] discovered peer\n");

	// Drive the state machine: Online -> Offline -> Online. Each transition after
	// the first requires BOTH the CopyPresence status flip AND an OnPresence
	// changed notification for this peer, so the callback path is proven too (and
	// the count is sampled without racing the dispatcher drain).
	int phase = 0; // 0=want online, 1=want offline, 2=want online again
	int sawOnline = 0, sawOffline = 0, sawOnlineAgain = 0, stillFriend = 1;
	int notifyAtOnline = 0, notifyAtOffline = 0;
	unsigned long long end = NOW_MS() + 50000;
	while (NOW_MS() < end && phase < 3)
	{
		Tick(1);
		if (!PeerIsFriend(peer)) stillFriend = 0; // must remain a friend throughout
		int st = PeerStatus(peer);
		if (phase == 0 && st == EOS_PS_Online)
		{
			sawOnline = 1; notifyAtOnline = gPeerNotify; phase = 1;
			printf("[watcher] peer online\n");
		}
		else if (phase == 1 && st == EOS_PS_Offline && gPeerNotify > notifyAtOnline)
		{
			sawOffline = 1; notifyAtOffline = gPeerNotify; phase = 2;
			printf("[watcher] peer offline (notify=%d)\n", gPeerNotify);
		}
		else if (phase == 2 && st == EOS_PS_Online && gPeerNotify > notifyAtOffline)
		{
			sawOnlineAgain = 1; phase = 3;
			printf("[watcher] peer back online (notify=%d)\n", gPeerNotify);
		}
	}

	int ok = sawOnline && sawOffline && sawOnlineAgain && stillFriend;
	printf("[watcher] online=%d offline=%d online2=%d friend=%d notify=%d\n",
		sawOnline, sawOffline, sawOnlineAgain, stillFriend, gPeerNotify);
	EOS_Platform_Release(gPlatform);
	EOS_Shutdown();
	printf("LIVENESS-WATCH %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
