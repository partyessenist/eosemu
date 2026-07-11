// RTC shim test for EOSEmu (single process).
//
// Exercises the voice-startup path a lobby-coupled game drives:
//   - CreateLobby(bEnableRTCRoom) -> RTCRoomConnectionChanged(connected),
//     ParticipantStatusChanged(Joined) for the local user, audio
//     ParticipantUpdated (muted, not speaking), data ParticipantUpdated
//     (enabled). The RTC notifications are registered *inside* the CreateLobby
//     completion -- the registration window eos_rtc.h documents.
//   - EOS_Lobby_IsRTCRoomConnected -> true while in the lobby.
//   - JoinRoom/LeaveRoom on the lobby-managed room -> EOS_AccessDenied.
//   - JoinRoom on a custom room -> Success + Joined for the local user;
//     LeaveRoom -> Success.
//   - RTCAudio UpdateSending mute toggle round-trips (Success + participant
//     update with the new status); device enumeration reports one fake
//     input/output device; RTCData SendData accepts in-room and NotFound
//     out-of-room; RTC SetSetting validates names.
//   - LeaveLobby -> RTCRoomConnectionChanged(disconnected).

#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_lobby.h"
#include "eos_rtc.h"
#include "eos_rtc_audio.h"
#include "eos_rtc_data.h"

static EOS_HPlatform gPlatform;
static EOS_HLobby gLobby;
static EOS_HRTC gRTC;
static EOS_HRTCAudio gAudio;
static EOS_HRTCData gData;

static int gConnectDone = 0;
static EOS_ProductUserId gLocalPuid = NULL;

static char gLobbyId[128];
static char gRoomName[256];
static int gLobbyCreated = 0;

static int gRoomConnected = 0;      // RTCRoomConnectionChanged(bIsConnected=1)
static int gRoomDisconnected = 0;   // RTCRoomConnectionChanged(bIsConnected=0)
static int gSelfJoined = 0;         // ParticipantStatusChanged Joined for local
static int gAudioMuted = 0;         // audio ParticipantUpdated: disabled + not speaking
static int gAudioUnmuted = 0;       // audio ParticipantUpdated: enabled (after UpdateSending)
static int gDataEnabled = 0;        // data ParticipantUpdated: enabled
static int gCustomJoined = 0;       // Joined for local in the custom room

static int gJoinLobbyRoomDone = 0, gJoinLobbyRoomDenied = 0;
static int gJoinCustomDone = 0, gJoinCustomOk = 0;
static int gLeaveCustomDone = 0, gLeaveCustomOk = 0;
static int gUpdateSendingDone = 0, gUpdateSendingOk = 0;

static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo* D)
{
	gLocalPuid = D->LocalUserId;
	gConnectDone = 1;
}

static void EOS_CALL OnRoomConn(const EOS_Lobby_RTCRoomConnectionChangedCallbackInfo* D)
{
	printf("  RTCRoomConnectionChanged lobby=%s connected=%d\n",
		D->LobbyId ? D->LobbyId : "?", (int)D->bIsConnected);
	if (D->bIsConnected) gRoomConnected = 1; else gRoomDisconnected = 1;
}

static void EOS_CALL OnParticipantStatus(const EOS_RTC_ParticipantStatusChangedCallbackInfo* D)
{
	printf("  ParticipantStatusChanged room=%s status=%d\n",
		D->RoomName ? D->RoomName : "?", (int)D->ParticipantStatus);
	if (D->ParticipantStatus == EOS_RTCPS_Joined && D->ParticipantId == gLocalPuid)
	{
		if (D->RoomName && strcmp(D->RoomName, gRoomName) == 0) gSelfJoined = 1;
		if (D->RoomName && strcmp(D->RoomName, "customroom") == 0) gCustomJoined = 1;
	}
}

static void EOS_CALL OnAudioParticipant(const EOS_RTCAudio_ParticipantUpdatedCallbackInfo* D)
{
	printf("  Audio ParticipantUpdated room=%s speaking=%d status=%d\n",
		D->RoomName ? D->RoomName : "?", (int)D->bSpeaking, (int)D->AudioStatus);
	if (D->ParticipantId != gLocalPuid) return;
	if (D->AudioStatus == EOS_RTCAS_Disabled && !D->bSpeaking) gAudioMuted = 1;
	if (D->AudioStatus == EOS_RTCAS_Enabled) gAudioUnmuted = 1;
}

static void EOS_CALL OnDataParticipant(const EOS_RTCData_ParticipantUpdatedCallbackInfo* D)
{
	printf("  Data ParticipantUpdated room=%s status=%d\n",
		D->RoomName ? D->RoomName : "?", (int)D->DataStatus);
	if (D->ParticipantId == gLocalPuid && D->DataStatus == EOS_RTCDS_Enabled) gDataEnabled = 1;
}

static void EOS_CALL OnCreateLobby(const EOS_Lobby_CreateLobbyCallbackInfo* D)
{
	if (D->ResultCode != EOS_Success || !D->LobbyId) return;
	strncpy(gLobbyId, D->LobbyId, sizeof(gLobbyId) - 1);
	gLobbyCreated = 1;
	printf("  created lobby %s\n", gLobbyId);

	// Resolve the room name and register RTC notifications right here, in the
	// completion -- the pattern eos_rtc.h documents as safe. The initial
	// Joined events are queued in this same drain and must still be seen.
	EOS_Lobby_GetRTCRoomNameOptions ro;
	memset(&ro, 0, sizeof(ro));
	ro.ApiVersion = EOS_LOBBY_GETRTCROOMNAME_API_LATEST;
	ro.LobbyId = gLobbyId;
	ro.LocalUserId = gLocalPuid;
	uint32_t len = sizeof(gRoomName);
	if (EOS_Lobby_GetRTCRoomName(gLobby, &ro, gRoomName, &len) != EOS_Success)
	{
		printf("  GetRTCRoomName failed\n");
		return;
	}
	printf("  RTC room name %s\n", gRoomName);

	EOS_RTC_AddNotifyParticipantStatusChangedOptions po;
	memset(&po, 0, sizeof(po));
	po.ApiVersion = EOS_RTC_ADDNOTIFYPARTICIPANTSTATUSCHANGED_API_LATEST;
	po.LocalUserId = gLocalPuid;
	po.RoomName = gRoomName;
	EOS_RTC_AddNotifyParticipantStatusChanged(gRTC, &po, NULL, &OnParticipantStatus);

	EOS_RTCAudio_AddNotifyParticipantUpdatedOptions ao;
	memset(&ao, 0, sizeof(ao));
	ao.ApiVersion = EOS_RTCAUDIO_ADDNOTIFYPARTICIPANTUPDATED_API_LATEST;
	ao.LocalUserId = gLocalPuid;
	ao.RoomName = gRoomName;
	EOS_RTCAudio_AddNotifyParticipantUpdated(gAudio, &ao, NULL, &OnAudioParticipant);

	EOS_RTCData_AddNotifyParticipantUpdatedOptions dopt;
	memset(&dopt, 0, sizeof(dopt));
	dopt.ApiVersion = EOS_RTCDATA_ADDNOTIFYPARTICIPANTUPDATED_API_LATEST;
	dopt.LocalUserId = gLocalPuid;
	dopt.RoomName = gRoomName;
	EOS_RTCData_AddNotifyParticipantUpdated(gData, &dopt, NULL, &OnDataParticipant);
}

static void EOS_CALL OnJoinLobbyRoom(const EOS_RTC_JoinRoomCallbackInfo* D)
{
	printf("  JoinRoom(lobby room) result=%s\n", EOS_EResult_ToString(D->ResultCode));
	gJoinLobbyRoomDone = 1;
	gJoinLobbyRoomDenied = (D->ResultCode == EOS_AccessDenied);
}

static void EOS_CALL OnJoinCustom(const EOS_RTC_JoinRoomCallbackInfo* D)
{
	printf("  JoinRoom(custom) result=%s\n", EOS_EResult_ToString(D->ResultCode));
	gJoinCustomDone = 1;
	gJoinCustomOk = (D->ResultCode == EOS_Success);
}

static void EOS_CALL OnLeaveCustom(const EOS_RTC_LeaveRoomCallbackInfo* D)
{
	printf("  LeaveRoom(custom) result=%s\n", EOS_EResult_ToString(D->ResultCode));
	gLeaveCustomDone = 1;
	gLeaveCustomOk = (D->ResultCode == EOS_Success);
}

static void EOS_CALL OnUpdateSending(const EOS_RTCAudio_UpdateSendingCallbackInfo* D)
{
	printf("  UpdateSending result=%s status=%d\n", EOS_EResult_ToString(D->ResultCode), (int)D->AudioStatus);
	gUpdateSendingDone = 1;
	gUpdateSendingOk = (D->ResultCode == EOS_Success && D->AudioStatus == EOS_RTCAS_Enabled);
}

static void Ticks(int n)
{
	for (int i = 0; i < n; ++i) EOS_Platform_Tick(gPlatform);
}

int main(void)
{
	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuRTC";
	InitOpts.ProductVersion = "1.0";
	if (EOS_Initialize(&InitOpts) != EOS_Success) { printf("Initialize failed\n"); return 1; }

	EOS_Platform_Options PlatOpts;
	memset(&PlatOpts, 0, sizeof(PlatOpts));
	PlatOpts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatOpts.ProductId = "prod";
	PlatOpts.SandboxId = "sand";
	PlatOpts.DeploymentId = "deploy";
	PlatOpts.ClientCredentials.ClientId = "cid";
	PlatOpts.ClientCredentials.ClientSecret = "secret";
	gPlatform = EOS_Platform_Create(&PlatOpts);
	if (!gPlatform) { printf("Platform_Create failed\n"); EOS_Shutdown(); return 2; }

	gLobby = EOS_Platform_GetLobbyInterface(gPlatform);
	gRTC = EOS_Platform_GetRTCInterface(gPlatform);
	gAudio = EOS_RTC_GetAudioInterface(gRTC);
	gData = EOS_RTC_GetDataInterface(gRTC);
	printf("RTC=%p Audio=%p Data=%p\n", (void*)gRTC, (void*)gAudio, (void*)gData);
	if (!gRTC || !gAudio || !gData) { printf("null RTC handles\n"); return 3; }

	EOS_HConnect Connect = EOS_Platform_GetConnectInterface(gPlatform);
	EOS_Connect_Credentials CCreds;
	memset(&CCreds, 0, sizeof(CCreds));
	CCreds.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	CCreds.Token = "eosemu-access-token";
	CCreds.Type = EOS_ECT_EPIC;
	EOS_Connect_LoginOptions CLogin;
	memset(&CLogin, 0, sizeof(CLogin));
	CLogin.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	CLogin.Credentials = &CCreds;
	EOS_Connect_Login(Connect, &CLogin, NULL, &OnConnectLogin);
	for (int i = 0; i < 50 && !gConnectDone; ++i) Ticks(1);
	if (!gLocalPuid) { printf("connect login failed\n"); return 3; }

	// Lobby-level RTC room connection notify, before the lobby exists.
	EOS_Lobby_AddNotifyRTCRoomConnectionChangedOptions co;
	memset(&co, 0, sizeof(co));
	co.ApiVersion = EOS_LOBBY_ADDNOTIFYRTCROOMCONNECTIONCHANGED_API_LATEST;
	EOS_Lobby_AddNotifyRTCRoomConnectionChanged(gLobby, &co, NULL, &OnRoomConn);

	// Create an RTC-enabled lobby.
	EOS_Lobby_CreateLobbyOptions lo;
	memset(&lo, 0, sizeof(lo));
	lo.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
	lo.LocalUserId = gLocalPuid;
	lo.MaxLobbyMembers = 4;
	lo.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
	lo.bAllowInvites = EOS_TRUE;
	lo.BucketId = "RTC:Test";
	lo.bEnableRTCRoom = EOS_TRUE;
	EOS_Lobby_CreateLobby(gLobby, &lo, NULL, &OnCreateLobby);
	for (int i = 0; i < 50 && !(gLobbyCreated && gRoomConnected && gSelfJoined && gAudioMuted && gDataEnabled); ++i) Ticks(1);

	// IsRTCRoomConnected must now agree.
	int isConnectedOk = 0;
	{
		EOS_Lobby_IsRTCRoomConnectedOptions io;
		memset(&io, 0, sizeof(io));
		io.ApiVersion = EOS_LOBBY_ISRTCROOMCONNECTED_API_LATEST;
		io.LobbyId = gLobbyId;
		io.LocalUserId = gLocalPuid;
		EOS_Bool Connected = EOS_FALSE;
		if (EOS_Lobby_IsRTCRoomConnected(gLobby, &io, &Connected) == EOS_Success)
			isConnectedOk = (Connected == EOS_TRUE);
		printf("IsRTCRoomConnected=%d\n", (int)Connected);
	}

	// Joining the lobby-managed room by hand is documented AccessDenied.
	EOS_RTC_JoinRoomOptions jo;
	memset(&jo, 0, sizeof(jo));
	jo.ApiVersion = EOS_RTC_JOINROOM_API_LATEST;
	jo.LocalUserId = gLocalPuid;
	jo.RoomName = gRoomName;
	jo.ClientBaseUrl = "https://example.invalid";
	EOS_RTC_JoinRoom(gRTC, &jo, NULL, &OnJoinLobbyRoom);
	for (int i = 0; i < 50 && !gJoinLobbyRoomDone; ++i) Ticks(1);

	// A custom (non-lobby) room join succeeds and reports the local user.
	EOS_RTC_AddNotifyParticipantStatusChangedOptions cpo;
	memset(&cpo, 0, sizeof(cpo));
	cpo.ApiVersion = EOS_RTC_ADDNOTIFYPARTICIPANTSTATUSCHANGED_API_LATEST;
	cpo.LocalUserId = gLocalPuid;
	cpo.RoomName = "customroom";
	EOS_RTC_AddNotifyParticipantStatusChanged(gRTC, &cpo, NULL, &OnParticipantStatus);

	jo.RoomName = "customroom";
	EOS_RTC_JoinRoom(gRTC, &jo, NULL, &OnJoinCustom);
	for (int i = 0; i < 50 && !(gJoinCustomDone && gCustomJoined); ++i) Ticks(1);

	// Unmute round-trip on the lobby room.
	EOS_RTCAudio_UpdateSendingOptions uso;
	memset(&uso, 0, sizeof(uso));
	uso.ApiVersion = EOS_RTCAUDIO_UPDATESENDING_API_LATEST;
	uso.LocalUserId = gLocalPuid;
	uso.RoomName = gRoomName;
	uso.AudioStatus = EOS_RTCAS_Enabled;
	EOS_RTCAudio_UpdateSending(gAudio, &uso, NULL, &OnUpdateSending);
	for (int i = 0; i < 50 && !(gUpdateSendingDone && gAudioUnmuted); ++i) Ticks(1);

	// One fake audio device each way.
	int devicesOk = 0;
	{
		EOS_RTCAudio_GetInputDevicesCountOptions gi;
		memset(&gi, 0, sizeof(gi));
		gi.ApiVersion = EOS_RTCAUDIO_GETINPUTDEVICESCOUNT_API_LATEST;
		EOS_RTCAudio_GetOutputDevicesCountOptions go;
		memset(&go, 0, sizeof(go));
		go.ApiVersion = EOS_RTCAUDIO_GETOUTPUTDEVICESCOUNT_API_LATEST;
		uint32_t inCount = EOS_RTCAudio_GetInputDevicesCount(gAudio, &gi);
		uint32_t outCount = EOS_RTCAudio_GetOutputDevicesCount(gAudio, &go);

		EOS_RTCAudio_CopyInputDeviceInformationByIndexOptions ci;
		memset(&ci, 0, sizeof(ci));
		ci.ApiVersion = EOS_RTCAUDIO_COPYINPUTDEVICEINFORMATIONBYINDEX_API_LATEST;
		ci.DeviceIndex = 0;
		EOS_RTCAudio_InputDeviceInformation* InDev = NULL;
		if (EOS_RTCAudio_CopyInputDeviceInformationByIndex(gAudio, &ci, &InDev) == EOS_Success && InDev)
		{
			printf("input device: %s (%s) default=%d\n", InDev->DeviceName, InDev->DeviceId, (int)InDev->bDefaultDevice);
			devicesOk = (inCount == 1 && outCount == 1 && InDev->bDefaultDevice == EOS_TRUE
				&& InDev->DeviceId && InDev->DeviceName);
			EOS_RTCAudio_InputDeviceInformation_Release(InDev);
		}
	}

	// Data sends: accepted in-room, NotFound out-of-room, oversize rejected.
	int dataOk = 0;
	{
		unsigned char Payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
		EOS_RTCData_SendDataOptions so;
		memset(&so, 0, sizeof(so));
		so.ApiVersion = EOS_RTCDATA_SENDDATA_API_LATEST;
		so.LocalUserId = gLocalPuid;
		so.RoomName = "customroom";
		so.Data = Payload;
		so.DataLengthBytes = sizeof(Payload);
		EOS_EResult inRoom = EOS_RTCData_SendData(gData, &so);
		so.RoomName = "not_a_room";
		EOS_EResult outRoom = EOS_RTCData_SendData(gData, &so);
		so.RoomName = "customroom";
		so.DataLengthBytes = EOS_RTCDATA_MAX_PACKET_SIZE + 1;
		EOS_EResult tooBig = EOS_RTCData_SendData(gData, &so);
		printf("SendData in=%s out=%s big=%s\n", EOS_EResult_ToString(inRoom),
			EOS_EResult_ToString(outRoom), EOS_EResult_ToString(tooBig));
		dataOk = (inRoom == EOS_Success && outRoom == EOS_NotFound && tooBig == EOS_InvalidParameters);
	}

	// Settings validation.
	int settingOk = 0;
	{
		EOS_RTC_SetSettingOptions so;
		memset(&so, 0, sizeof(so));
		so.ApiVersion = EOS_RTC_SETSETTING_API_LATEST;
		so.SettingName = "DisableDtx";
		so.SettingValue = "True";
		EOS_EResult known = EOS_RTC_SetSetting(gRTC, &so);
		so.SettingName = "NoSuchSetting";
		EOS_EResult unknown = EOS_RTC_SetSetting(gRTC, &so);
		printf("SetSetting known=%s unknown=%s\n", EOS_EResult_ToString(known), EOS_EResult_ToString(unknown));
		settingOk = (known == EOS_Success && unknown == EOS_NotFound);
	}

	// Leave the custom room; leaving the lobby room by hand stays denied.
	EOS_RTC_LeaveRoomOptions lro;
	memset(&lro, 0, sizeof(lro));
	lro.ApiVersion = EOS_RTC_LEAVEROOM_API_LATEST;
	lro.LocalUserId = gLocalPuid;
	lro.RoomName = "customroom";
	EOS_RTC_LeaveRoom(gRTC, &lro, NULL, &OnLeaveCustom);
	for (int i = 0; i < 50 && !gLeaveCustomDone; ++i) Ticks(1);

	// Leaving the lobby disconnects its RTC room.
	EOS_Lobby_LeaveLobbyOptions llo;
	memset(&llo, 0, sizeof(llo));
	llo.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
	llo.LocalUserId = gLocalPuid;
	llo.LobbyId = gLobbyId;
	EOS_Lobby_LeaveLobby(gLobby, &llo, NULL, NULL);
	for (int i = 0; i < 50 && !gRoomDisconnected; ++i) Ticks(1);

	EOS_Platform_Release(gPlatform);
	EOS_Shutdown();

	printf("conn=%d self=%d muted=%d data=%d isconn=%d denied=%d custom=%d(%d) unmute=%d(%d) dev=%d send=%d set=%d leave=%d disc=%d\n",
		gRoomConnected, gSelfJoined, gAudioMuted, gDataEnabled, isConnectedOk,
		gJoinLobbyRoomDenied, gJoinCustomOk, gCustomJoined, gUpdateSendingOk, gAudioUnmuted,
		devicesOk, dataOk, settingOk, gLeaveCustomOk, gRoomDisconnected);
	int ok = gRoomConnected && gSelfJoined && gAudioMuted && gDataEnabled && isConnectedOk
		&& gJoinLobbyRoomDenied && gJoinCustomOk && gCustomJoined
		&& gUpdateSendingOk && gAudioUnmuted && devicesOk && dataOk && settingOk
		&& gLeaveCustomOk && gRoomDisconnected;
	printf("RTC %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 4;
}
