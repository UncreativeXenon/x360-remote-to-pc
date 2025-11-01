#include <iostream>
#include <xtl.h>
#include <xkelib.h>
#include <xhttp.h>
#include <xboxmath.h>
#include <xdk.h>
#include <winsockx.h>
#include <stdio.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
#include <xgraphics.h>
#include "Detour.h"
#include <io.h> 
#include <direct.h>
#include <sys/stat.h>
#include <winnt.h>
#include <xbox.h>
#include <stdint.h>
#include <ctype.h>
#include <stddef.h>
#pragma comment(lib, "xnet.lib")

Detour XamInputGetStateDetour;
void* XamInputGetState = nullptr;

SOCKET g_sock = INVALID_SOCKET;

PLDR_DATA_TABLE_ENTRY pDataTable = nullptr;

char pluginPath[MAX_PATH];

char ip[64] = "127.0.0.1";
int port = 4000;
bool disableConsoleInput = true;
int maxControllers = 4;

bool gotIp = false;

#pragma pack(push, 1)
struct X360Packet {
	DWORD userIndex;
	WORD wButtons;
	BYTE bLeftTrigger;
	BYTE bRightTrigger;
	SHORT sThumbLX;
	SHORT sThumbLY;
	SHORT sThumbRX;
	SHORT sThumbRY;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct X360Notification
{
    DWORD userIndex;
    UCHAR largeMotor;
    UCHAR smallMotor;
};
#pragma pack(pop)

bool sendPing()
{
    if (g_sock == INVALID_SOCKET) return false;

	int res = NetDll_send(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock, "ping", strlen("ping"), 0);

    if (res == SOCKET_ERROR)
    {
        DbgPrint("Ping failed, socket disconnected: %d\n", NetDll_WSAGetLastError());
		XamInputGetStateDetour.Remove();
        NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }

    return true;
}

bool ReadConfig()
{
    if (gotIp) return true;

    FILE* file = fopen(pluginPath, "r");
    if (!file)
    {
        OutputDebugStringA("Failed to open config file.\n");
        return false;
    }

    char buffer[256];

    while (fgets(buffer, sizeof(buffer), file))
    {
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strlen(buffer) == 0 || buffer[0] == '#')
            continue;

        char lower[256];
        strcpy(lower, buffer);
        for (char* p = lower; *p; ++p)
            *p = (char)tolower(*p);

        if (strncmp(lower, "ip=", 3) == 0)
        {
            strcpy(ip, buffer + 3);
        }
        else if (strncmp(lower, "port=", 5) == 0)
        {
            port = atoi(buffer + 5);
        }
        else if (strncmp(lower, "disableconsoleinput=", 20) == 0)
        {
            const char* val = buffer + 20;
            disableConsoleInput = (_stricmp(val, "true") == 0 || strcmp(val, "1") == 0);
        }
        else if (strncmp(lower, "maxcontrollers=", 15) == 0)
        {
            maxControllers = atoi(buffer + 15);
        }
    }

    fclose(file);
    gotIp = true;

    char debugMsg[256];
    sprintf(debugMsg, "Config loaded:\nIP=%s\nPort=%d\nDisableConsoleInput=%d\nMaxControllers=%d\n",
        ip, port, disableConsoleInput, maxControllers);
    OutputDebugStringA(debugMsg);

    return TRUE;
}

DWORD* XampInputRoutedToSysapp = nullptr;

DWORD XamInputGetStateHook(DWORD userIndex, DWORD flags, XINPUT_STATE* input_state) {
	DWORD status = XamInputGetStateDetour.GetOriginal<decltype(&XamInputGetStateHook)>()(userIndex, flags, input_state);

	XINPUT_GAMEPAD gamepad = input_state->Gamepad;

	X360Packet packet;
	packet.userIndex = userIndex;
	packet.wButtons = gamepad.wButtons;
	packet.bLeftTrigger = gamepad.bLeftTrigger;
	packet.bRightTrigger = gamepad.bRightTrigger;
	packet.sThumbLX = gamepad.sThumbLX;
	packet.sThumbLY = gamepad.sThumbLY;
	packet.sThumbRX = gamepad.sThumbRX;
	packet.sThumbRY = gamepad.sThumbRY;

	if (g_sock != INVALID_SOCKET && userIndex < static_cast<unsigned int>(maxControllers)) {
		if (userIndex < static_cast<unsigned int>(maxControllers)) {
			NetDll_send(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock, reinterpret_cast<const char*>(&packet), sizeof(packet), 0);
		}

		if (disableConsoleInput) {
			input_state->Gamepad.wButtons = 0;
			input_state->Gamepad.sThumbRX = 0;
			input_state->Gamepad.sThumbRY = 0;
			input_state->Gamepad.sThumbLX = 0;
			input_state->Gamepad.sThumbLY = 0;
			input_state->Gamepad.bLeftTrigger = 0;
			input_state->Gamepad.bRightTrigger = 0;
		}

		return ERROR_SUCCESS;
	}

	return status;
}

bool initSocket() {
    XNetStartupParams xnsp;
    memset(&xnsp, 0, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(XNetStartupParams);
    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        DbgPrint("WSAStartup failed\n");
        return 0;
    }

	Sleep(6000);

	if (!ReadConfig()) return false;

	g_sock = NetDll_socket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) {
        DbgPrint("socket creation failed: %d\n", NetDll_WSAGetLastError());
        NetDll_WSACleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
        return 0;
    }
    DbgPrint("Socket created\n");

	BOOL opt_true = TRUE;
	NetDll_setsockopt(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock, SOL_SOCKET, 0x5801, (PCSTR)&opt_true, sizeof(BOOL));

    SOCKADDR_IN target;
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = inet_addr(ip);

	if (NetDll_connect(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock, (SOCKADDR*)&target, sizeof(target)) == SOCKET_ERROR) {
        DbgPrint("connect failed: %d\n", NetDll_WSAGetLastError());
        NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock);
        NetDll_WSACleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
        return 0;
    }
    DbgPrint("Connected to server\n");

	char wsHandshake[512];
	sprintf(
		wsHandshake,
		"GET / HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n",
		ip, port
	);

    if (NetDll_send(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock, wsHandshake, strlen(wsHandshake), 0) == SOCKET_ERROR) {
        DbgPrint("Handshake send failed: %d\n", NetDll_WSAGetLastError());
        NetDll_closesocket(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock);
        NetDll_WSACleanup(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP));
        return 0;
    }
    DbgPrint("Handshake sent\n");

	XamInputGetStateDetour = Detour(XamInputGetState, (void*)XamInputGetStateHook);
	if (!XamInputGetStateDetour.Install()) {
		DbgPrint("Failed to install hook\n");
	} else {
		DbgPrint("Hook installed safely\n");
	}

    return true;
}

bool initFunctionPointers() {
	HANDLE xamHandle = GetModuleHandleA("xam.xex");

	XexGetProcedureAddress(xamHandle, 401, &XamInputGetState);

	return true;
}

void reconnect()
{
    while (!initSocket())
    {
        DbgPrint("Reconnect failed, retrying in 5 seconds...\n");
        Sleep(5000);
    }

    DbgPrint("Reconnected successfully!\n");
}

DWORD WINAPI HeartbeatThread(void* param)
{
    while (true)
    {
        if (!sendPing())
        {
            reconnect();
        }
        Sleep(10000);
    }
    return 0;
}

DWORD WINAPI HookThread(void* param) {
	if (!initFunctionPointers())
		return FALSE;

	WORD lastSent = 0;

	HANDLE hPingThread;

	initSocket();

	ExCreateThread(&hPingThread, 0, nullptr, nullptr, HeartbeatThread, nullptr, 0);

	X360Notification notif;
	XINPUT_VIBRATION vibration;

	while (true)
    {
		if (g_sock != INVALID_SOCKET)
        {
			int total = 0;
			char* ptr = reinterpret_cast<char*>(&notif);
			while (total < sizeof(notif))
			{
				int res = NetDll_recv(static_cast<XNCALLER_TYPE>(XNCALLER_SYSAPP), g_sock, ptr + total, sizeof(notif) - total, 0);
				if (res != 0 && res != SOCKET_ERROR)
				{
					total += res;
				}
			}

			vibration.wLeftMotorSpeed = static_cast<WORD>(notif.largeMotor  * 257);
			vibration.wRightMotorSpeed = static_cast<WORD>(notif.smallMotor * 257);

			//DWORD result = XInputSetState(ntohl(notif.userIndex), &vibration);
			DWORD result;
			do {
				result = XInputSetState(ntohl(notif.userIndex), &vibration);
			} while (result == ERROR_BUSY);
		}

		Sleep(20);
    }

    return 0;
}

BOOL DllMain(HINSTANCE hModule, DWORD reason, void* pReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        HANDLE hThread;

		LDR_DATA_TABLE_ENTRY *pDataTable = reinterpret_cast<LDR_DATA_TABLE_ENTRY *>(hModule);

        WideCharToMultiByte(CP_ACP, 0, pDataTable->FullDllName.Buffer, -1, pluginPath, MAX_PATH, nullptr, nullptr);

        char* lastSlash = strrchr(pluginPath, '\\');
        if (lastSlash)
        {
            *(lastSlash + 1) = '\0';
        }

		DbgPrint("PATH: %s", pluginPath);
		
		const char* oldPrefix = "\\Device\\Harddisk0\\Partition1\\";
        const char* newPrefix = "hdd:\\";
        if (strncmp(pluginPath, oldPrefix, strlen(oldPrefix)) == 0)
        {
            char temp[MAX_PATH];
            strcpy(temp, pluginPath + strlen(oldPrefix));
            strcpy(pluginPath, newPrefix);
            strcat(pluginPath, temp);
        }

		const char* oldPrefix1 = "\\Device\\Mass0\\";
        const char* newPrefix1 = "usb0:\\";
        if (strncmp(pluginPath, oldPrefix1, strlen(oldPrefix1)) == 0)
        {
            char temp[MAX_PATH];
            strcpy(temp, pluginPath + strlen(oldPrefix1));
            strcpy(pluginPath, newPrefix1);
            strcat(pluginPath, temp);
        }

		const char* oldPrefix2 = "\\Device\\Mass1\\";
        const char* newPrefix2 = "usb1:\\";
        if (strncmp(pluginPath, oldPrefix2, strlen(oldPrefix2)) == 0)
        {
            char temp[MAX_PATH];
            strcpy(temp, pluginPath + strlen(oldPrefix2));
            strcpy(pluginPath, newPrefix2);
            strcat(pluginPath, temp);
        }

		const char* oldPrefix3 = "\\Device\\Mass2\\";
        const char* newPrefix3 = "usb2:\\";
        if (strncmp(pluginPath, oldPrefix3, strlen(oldPrefix3)) == 0)
        {
            char temp[MAX_PATH];
            strcpy(temp, pluginPath + strlen(oldPrefix3));
            strcpy(pluginPath, newPrefix3);
            strcat(pluginPath, temp);
        }

		strcat(pluginPath, "360ControllerToPC.ini");

        ExCreateThread(&hThread, 0, nullptr, nullptr, HookThread, nullptr, 2);
        break;
    }

    case DLL_PROCESS_DETACH:
        XamInputGetStateDetour.Remove();
		XamInputGetState = nullptr;
		g_sock = INVALID_SOCKET;
		pDataTable = nullptr;
		memset(pluginPath, 0, sizeof(pluginPath));
		memset(ip, 0, sizeof(ip));
		port = 4000;
		gotIp = false;
		maxControllers = 4;
		disableConsoleInput = true;
        break;
    }

    return TRUE;
}