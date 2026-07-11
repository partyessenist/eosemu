// Two-instance presence replication test.
//
// Run as two processes on the same machine (distinct EOSEMU_PROFILE):
//   setter:  sets rich presence (status/richtext/joininfo/data records) via the
//            modification builder, then keeps announcing for ~15s.
//   checker: discovers the setter through the friends list, then polls
//            EOS_Presence_CopyPresence until the replicated presence arrives:
//            Status=Away, RichText, ProductId equal to its OWN product id (the
//            "friend is in-game" signal), the activity data record, and
//            EOS_Presence_GetJoinInfo returning the setter's join info. Also
//            asserts HasPresence and that AddNotifyOnPresenceChanged fired.
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
#include "eos_friends.h"
#include "eos_presence.h"

#define PRODUCT_ID   "prod"
#define RICH_TEXT    "CoopRun"
#define JOIN_INFO    "join:lobby42"
#define ACT_KEY      "activity_room_id"
#define ACT_VALUE    "room42"

static EOS_HPlatform gPlatform;
static EOS_HPresence gPresence;
static EOS_HFriends gFriends;
static EOS_ProductUserId gLocalPuid;
static int gConnectDone;
static int gSetDone;
static int gSetOk;
static int gNotifyCount;

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

static void EOS_CALL OnSetPresence(const EOS_Presence_SetPresenceCallbackInfo* D)
{
	gSetDone = 1;
	gSetOk = (D->ResultCode == EOS_Success);
}

static void EOS_CALL OnPresenceChanged(const EOS_Presence_PresenceChangedCallbackInfo* D)
{
	(void)D;
	++gNotifyCount;
}

int main(int argc, char** argv)
{
	const char* role = argc > 1 ? argv[1] : "setter";
	int isSetter = strcmp(role, "setter") == 0;

	EOS_InitializeOptions io; memset(&io, 0, sizeof(io));
	io.ApiVersion = EOS_INITIALIZE_API_LATEST;
	io.ProductName = "EOSEmuPresence"; io.ProductVersion = "2.5";
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

	if (isSetter)
	{
		EOS_Presence_CreatePresenceModificationOptions co = {0};
		co.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
		EOS_HPresenceModification mod = NULL;
		if (EOS_Presence_CreatePresenceModification(gPresence, &co, &mod) != EOS_Success || !mod)
		{
			printf("PRESENCE-SET FAIL (create modification)\n");
			return 3;
		}
		EOS_PresenceModification_SetStatusOptions so = {0};
		so.ApiVersion = EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST;
		so.Status = EOS_PS_Away;
		EOS_PresenceModification_SetStatus(mod, &so);
		EOS_PresenceModification_SetRawRichTextOptions ro = {0};
		ro.ApiVersion = EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;
		ro.RichText = RICH_TEXT;
		EOS_PresenceModification_SetRawRichText(mod, &ro);
		EOS_PresenceModification_SetJoinInfoOptions jo = {0};
		jo.ApiVersion = EOS_PRESENCEMODIFICATION_SETJOININFO_API_LATEST;
		jo.JoinInfo = JOIN_INFO;
		EOS_PresenceModification_SetJoinInfo(mod, &jo);
		EOS_Presence_DataRecord rec[2];
		memset(rec, 0, sizeof(rec));
		rec[0].ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
		rec[0].Key = ACT_KEY; rec[0].Value = ACT_VALUE;
		rec[1].ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
		rec[1].Key = "mode"; rec[1].Value = "coop";
		EOS_PresenceModification_SetDataOptions dro = {0};
		dro.ApiVersion = EOS_PRESENCEMODIFICATION_SETDATA_API_LATEST;
		dro.RecordsCount = 2; dro.Records = rec;
		EOS_PresenceModification_SetData(mod, &dro);

		EOS_Presence_SetPresenceOptions spo = {0};
		spo.ApiVersion = EOS_PRESENCE_SETPRESENCE_API_LATEST;
		spo.PresenceModificationHandle = mod;
		EOS_Presence_SetPresence(gPresence, &spo, NULL, &OnSetPresence);
		EOS_PresenceModification_Release(mod);
		for (int i = 0; i < 50 && !gSetDone; ++i) Tick(1);

		// Keep announcing so the checker can converge. Wall-clock bound: SLEEP_MS
		// rounds up to the OS timer granularity, so counting ticks overshoots.
		{
#if defined(_WIN32)
			ULONGLONG end = GetTickCount64() + 12000;
			while (GetTickCount64() < end) Tick(1);
#else
			time_t end = time(NULL) + 12;
			while (time(NULL) < end) Tick(1);
#endif
		}

		int ok = gSetDone && gSetOk;
		printf("[setter] set=%d ok=%d\n", gSetDone, gSetOk);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("PRESENCE-SET %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
	else
	{
		EOS_Presence_AddNotifyOnPresenceChangedOptions no = {0};
		no.ApiVersion = EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST;
		EOS_Presence_AddNotifyOnPresenceChanged(gPresence, &no, NULL, &OnPresenceChanged);

		// Wait for the setter to appear as a friend.
		EOS_EpicAccountId peer = NULL;
		for (int i = 0; i < 500 && !peer; ++i)
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
		if (!peer)
		{
			printf("PRESENCE-CHECK FAIL (no peer discovered)\n");
			EOS_Platform_Release(gPlatform); EOS_Shutdown();
			return 3;
		}
		printf("[checker] discovered peer\n");

		// Poll CopyPresence until the replicated presence arrives.
		int statusOk = 0, richOk = 0, productOk = 0, recordOk = 0, joinOk = 0, hasOk = 0;
		for (int i = 0; i < 500 && !(statusOk && richOk && productOk && recordOk && joinOk); ++i)
		{
			Tick(1);
			statusOk = richOk = productOk = recordOk = 0;
			EOS_Presence_CopyPresenceOptions cpo = {0};
			cpo.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
			cpo.TargetUserId = peer;
			EOS_Presence_Info* info = NULL;
			if (EOS_Presence_CopyPresence(gPresence, &cpo, &info) == EOS_Success && info)
			{
				statusOk = (info->Status == EOS_PS_Away);
				richOk = (info->RichText && strcmp(info->RichText, RICH_TEXT) == 0);
				productOk = (info->ProductId && strcmp(info->ProductId, PRODUCT_ID) == 0);
				for (int32_t r = 0; r < info->RecordsCount; ++r)
					if (info->Records[r].Key && strcmp(info->Records[r].Key, ACT_KEY) == 0
						&& info->Records[r].Value && strcmp(info->Records[r].Value, ACT_VALUE) == 0)
						recordOk = 1;
				EOS_Presence_Info_Release(info);
			}
			char join[128]; int32_t len = sizeof(join);
			EOS_Presence_GetJoinInfoOptions gjo = {0};
			gjo.ApiVersion = EOS_PRESENCE_GETJOININFO_API_LATEST;
			gjo.TargetUserId = peer;
			joinOk = (EOS_Presence_GetJoinInfo(gPresence, &gjo, join, &len) == EOS_Success
				&& strcmp(join, JOIN_INFO) == 0);
		}
		EOS_Presence_HasPresenceOptions hpo = {0};
		hpo.ApiVersion = EOS_PRESENCE_HASPRESENCE_API_LATEST;
		hpo.TargetUserId = peer;
		hasOk = (EOS_Presence_HasPresence(gPresence, &hpo) == EOS_TRUE);

		int ok = statusOk && richOk && productOk && recordOk && joinOk && hasOk && gNotifyCount > 0;
		printf("[checker] status=%d rich=%d product=%d record=%d join=%d has=%d notify=%d\n",
			statusOk, richOk, productOk, recordOk, joinOk, hasOk, gNotifyCount);
		EOS_Platform_Release(gPlatform); EOS_Shutdown();
		printf("PRESENCE-CHECK %s\n", ok ? "PASS" : "FAIL");
		return ok ? 0 : 3;
	}
}
