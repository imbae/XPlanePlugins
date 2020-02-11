#include "XPLMPlugin.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMMenus.h"
#include "XPLMUtilities.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPLMDataAccess.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <time.h> 
#include <winsock2.h>
#if IBM
#include <windows.h>
#endif
#include <vector>

static WSADATA wsaData; // 윈속 데이터 구조체.(WSAStartup() 사용할꺼!)
static SOCKET ServerSocket; // 소켓 선언
static SOCKADDR_IN ServerInfo; // 서버 주소정보 구조체
static SOCKADDR_IN FromClient; // 클라이언트에서 받는 주소정보 구조체

static int FromClient_Size; // 클라이언트로부터 받는 메시지 크기
static int Recv_Size; // 받는 사이즈
static int Send_Size; // 보내는 사이즈
static int Count;

static char Buffer[48];
static short ServerPort = 50000; // 서버 포트번호


static int MenuItem1;

static float	MyFlightLoopCallback(
	float                inElapsedSinceLastCall,
	float                inElapsedTimeSinceLastFlightLoop,
	int                  inCounter,
	void* inRefcon);

static XPLMDataRef gSpecialDataRef;

static XPLMDataRef		gPlaneX = NULL;
static XPLMDataRef		gPlaneY = NULL;
static XPLMDataRef		gPlaneZ = NULL;

static XPLMDataRef		gPlaneLat = NULL;
static XPLMDataRef		gPlaneLon = NULL;
static XPLMDataRef		gPlaneAlt = NULL;
static XPLMDataRef		gPlaneTheta = NULL;
static XPLMDataRef		gPlanePhi = NULL;
static XPLMDataRef		gPlanePsi = NULL;


static XPWidgetID PilsWidget = NULL;
static XPWidgetID PilsWindow = NULL;

static XPWidgetID OverrideEdit = NULL;
static XPWidgetID OverrideCheckBox = NULL;
static XPWidgetID PilsStartButton = NULL;

static XPWidgetID			Position2Text[3] = { NULL };
static XPWidgetID			Position2Edit[3] = { NULL };

static void PilsMenuHandler(void*, void*);
static void CreatePilsWindow(int x1, int y1, int w, int h);
static int PilsHandler(XPWidgetMessage inMessage,XPWidgetID inWidget,intptr_t inParam1,intptr_t inParam2);
static void SetDataRefState(XPLMDataRef, int);

static void StartPILS(void);

inline	float	HACKFLOAT(float val)
{
	return val;
}

PLUGIN_API int XPluginStart(
	char* outName,
	char* outSig,
	char* outDesc)
{
	XPLMMenuID	id;
	int			item;

	strcpy(outName, "PILS");
	strcpy(outSig, "xpsdk.custom.pils");
	strcpy(outDesc, "Brains Lab PILS Interface");

	item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "PILS", NULL, 1);

	id = XPLMCreateMenu("PILS", XPLMFindPluginsMenu(), item, PilsMenuHandler, NULL);
	XPLMAppendMenuItem(id, "Setting", (void*)"PILS", 1);

	MenuItem1 = 0;

	/* Prefetch the sim variables we will use. */
	gPlaneLat = XPLMFindDataRef("sim/flightmodel/position/latitude");
	gPlaneLon = XPLMFindDataRef("sim/flightmodel/position/longitude");
	gPlaneAlt = XPLMFindDataRef("sim/flightmodel/position/elevation");
	gPlaneX = XPLMFindDataRef("sim/flightmodel/position/local_x");
	gPlaneY = XPLMFindDataRef("sim/flightmodel/position/local_y");
	gPlaneZ = XPLMFindDataRef("sim/flightmodel/position/local_z");
	gPlaneTheta = XPLMFindDataRef("sim/flightmodel/position/theta");
	gPlanePhi = XPLMFindDataRef("sim/flightmodel/position/phi");
	gPlanePsi = XPLMFindDataRef("sim/flightmodel/position/psi");
	
	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	XPLMUnregisterFlightLoopCallback(MyFlightLoopCallback, NULL);

	if (MenuItem1 == 1)
	{
		XPDestroyWidget(PilsWidget, 1);
		MenuItem1 = 0;
	}
}

PLUGIN_API void XPluginDisable(void)
{
}

PLUGIN_API int XPluginEnable(void)
{
	return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void* inParam)
{
}

void PilsMenuHandler(void* mRef, void* iRef)
{
	if (!strcmp((char*)iRef, "PILS"))
	{
		if (MenuItem1 == 0)
		{
			CreatePilsWindow(280, 130, 280, 130);
			MenuItem1 = 1;
		}
		else
			if (!XPIsWidgetVisible(PilsWidget))
				XPShowWidget(PilsWidget);
	}
}


void CreatePilsWindow(int x, int y, int w, int h)
{
	int x2 = x + w;
	int y2 = y - h;
	
	/////////////////////////////////////////////////////////////////////////////////////////////////

	PilsWidget = XPCreateWidget(x, y, x2, y2,
		1,	// Visible
		"PILS",	// desc
		1,		// root
		NULL,	// no container
		xpWidgetClass_MainWindow);

	XPSetWidgetProperty(PilsWidget, xpProperty_MainWindowHasCloseBoxes, 1);

	PilsWindow = XPCreateWidget(x + 20, y - 30, x2 - 20, y2 + 20,
		1,	// Visible
		"",	// desc
		0,		// root
		PilsWidget,
		xpWidgetClass_SubWindow);

	XPSetWidgetProperty(PilsWindow, xpProperty_SubWindowType, xpSubWindowStyle_SubWindow);

	///////////////////////////////////////////////////////////////////////////////////////////////////

	OverrideEdit = XPCreateWidget(x + 40, y - 50, x + 190, y - 60,
		1, "Override Plane Path", 0, PilsWidget,
		xpWidgetClass_TextField);
	XPSetWidgetProperty(OverrideEdit, xpProperty_TextFieldType, xpTextEntryField);


	OverrideCheckBox = XPCreateWidget(x + 220, y - 50, x + 240, y - 60,
		1, "", 0, PilsWidget,
		xpWidgetClass_Button);

	XPSetWidgetProperty(OverrideCheckBox, xpProperty_ButtonType, xpRadioButton);
	XPSetWidgetProperty(OverrideCheckBox, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox);
	XPSetWidgetProperty(OverrideCheckBox, xpProperty_ButtonState, 0);

	////////////////////////////////////////////////////////////////////////////////////////////////////////

	PilsStartButton = XPCreateWidget(x + 40, y - 80, x + 240, y - 100,
		1, "PILS Start", 0, PilsWidget,
		xpWidgetClass_Button);

	XPSetWidgetProperty(PilsStartButton, xpProperty_ButtonType, xpPushButton);

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	XPAddWidgetCallback(PilsWidget, PilsHandler);
}

//데이터 씹힘 방지 위한 이중 장치
void SetDataRefState(XPLMDataRef DataRefID, int State)
{
	int	IntVals[8];
	memset(IntVals, 0, sizeof(IntVals));

	gSpecialDataRef = XPLMFindDataRef("sim/operation/override/override_planepath");

	if (DataRefID == gSpecialDataRef)
	{
		IntVals[0] = State;
		XPLMSetDatavi(DataRefID, IntVals, 0, 8);
	}
	else
		XPLMSetDatai(DataRefID, State);
}


static double latCount;
static double lonCount;
static double altCount;

float	MyFlightLoopCallback(
	float                inElapsedSinceLastCall,
	float                inElapsedTimeSinceLastFlightLoop,
	int                  inCounter,
	void* inRefcon)
{
	FromClient_Size = sizeof(FromClient);

	Recv_Size = recvfrom(ServerSocket, Buffer, 48, 0,
		(struct sockaddr*) & FromClient, &FromClient_Size);

	char tempLat[16];
	char tempLon[16];
	char tempAlt[16];

	for (int i = 0; i < 16; i++)
	{
		tempLat[i] = Buffer[i];
	}

	for (int i = 0; i < 16; i++)
	{
		tempLon[i] = Buffer[i + 16];
	}

	for (int i = 0; i < 16; i++)
	{
		tempAlt[i] = Buffer[i + 32];
	}

	double lat, lon, alt;
	double x, y, z;

	lat = atof(tempLat);
	lon = atof(tempLon);
	alt = atof(tempAlt);

	/*lat = atof("37.6286540");
	lon = atof("-122.3932880");
	alt = atof("50");

	lat += latCount;
	lon += lonCount;
	alt += altCount;

	latCount += 0.000001f;
	lonCount += 0.000001f;
	altCount += 0.1f;*/

	XPLMWorldToLocal(lat, lon, alt / 3.28, &x, &y, &z);

	XPLMSetDataf(gPlaneLat, lat);
	XPLMSetDataf(gPlaneX, x);
	XPLMSetDataf(gPlaneLon, lon);
	XPLMSetDataf(gPlaneY, y);
	XPLMSetDataf(gPlaneAlt, alt);
	XPLMSetDataf(gPlaneZ, z);

	/* Return 1.0 to indicate that we want to be called again in 1 second. */
	return 0.01f;
}

int	PilsHandler(XPWidgetMessage inMessage, XPWidgetID inWidget, intptr_t inParam1, intptr_t inParam2)
{
	if (inMessage == xpMessage_CloseButtonPushed)
	{
		if (MenuItem1 == 1)
		{
			XPHideWidget(PilsWidget);
		}
		return 1;
	}

	if (inMessage == xpMsg_PushButtonPressed)
	{
		if (inParam1 == (intptr_t)PilsStartButton)
		{
			StartPILS();
			return 1;
		}
	}

	if (inMessage == xpMsg_ButtonStateChanged)
	{
		XPLMDataRef DataRefID;
		DataRefID = XPLMFindDataRef("sim/operation/override/override_planepath");

		intptr_t State = XPGetWidgetProperty(OverrideCheckBox, xpProperty_ButtonState, 0);

		SetDataRefState(DataRefID, (int)State);
	}


	return 0;
}

void StartPILS(void)
{
	//WSAStartup은 WS2_32.DLL을 초기화 하는 함수
	if (WSAStartup(0x202, &wsaData) == SOCKET_ERROR) // WSAStartup 설정에서 문제 발생하면
	{
		WSACleanup(); // WS2_32.DLL의 사용 끝냄
	}

	// memset : 사용자가 정한 메모리 크기만큼 메모리 영역을 특정한 값으로 채움
	memset(&ServerInfo, 0, sizeof(ServerInfo)); // 0으로 초기화
	memset(&FromClient, 0, sizeof(FromClient));
	memset(Buffer, 0, 48);

	ServerInfo.sin_family = AF_INET; // IPv4 주소체계 사용 
	ServerInfo.sin_addr.s_addr = inet_addr("127.0.0.1"); // 루프백 IP. 즉 혼자놀이용..
	ServerInfo.sin_port = htons(ServerPort); // 포트번호

	// 소켓 생성
	ServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // udp용 소켓 생성. SOCK_DGRAM : UDP 사용
	if (ServerSocket == INVALID_SOCKET) // 에러 발생시
	{
		closesocket(ServerSocket);
		WSACleanup();
		exit(0);
	}

	// bind() - 새로 오는 클라이언트를 받을 welcome 소켓
	if (bind(ServerSocket, (struct sockaddr*) & ServerInfo, //바인드 소켓에 서버정보 부여
		sizeof(ServerInfo)) == SOCKET_ERROR)
	{
		closesocket(ServerSocket);
		WSACleanup();
		exit(0);
	}

	/* Register our callback for once a second.  Positive intervals
	 * are in seconds, negative are the negative of sim frames.  Zero
	 * registers but does not schedule a callback for time. */
	XPLMRegisterFlightLoopCallback(
		MyFlightLoopCallback,	/* Callback */
		0.01f,					/* Interval */
		NULL);					/* refcon not used. */
}