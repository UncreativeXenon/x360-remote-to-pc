#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <ViGEm/Client.h>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

#include <array>

PVIGEM_TARGET runningPads[4];

XUSB_REPORT report;

std::atomic<bool> running(true);
std::atomic<std::chrono::steady_clock::time_point> lastPingTime;

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

uint32_t SwapDWORD(uint32_t val)
{
    return ((val & 0x000000FF) << 24) |  // move byte 0 to byte 3
        ((val & 0x0000FF00) << 8) |  // move byte 1 to byte 2
        ((val & 0x00FF0000) >> 8) |  // move byte 2 to byte 1
        ((val & 0xFF000000) >> 24);   // move byte 3 to byte 0
}

VOID CALLBACK notification(
    PVIGEM_CLIENT Client,
    PVIGEM_TARGET Target,
    UCHAR LargeMotor,
    UCHAR SmallMotor,
    UCHAR LedNumber,
    LPVOID UserData
)
{
    DWORD userIndex = -1;

    for (int i = 0; i < 4; i++)
    {
        if (runningPads[i] == Target) {
            userIndex = i;
        }
    }

    X360Notification notif = { htonl(userIndex), LargeMotor, SmallMotor };

    SOCKET sock = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(UserData));

    if (userIndex != -1) {
        send(sock, reinterpret_cast<const char*>(&notif), sizeof(notif), 0);
    }
}

/*void watchdog()
{
    while (running)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastPingTime.load()).count();

        if (elapsed >= 20)
        {
            std::cout << "[Watchdog] Timeout: no ping for 20s! Restarting logic...\n";
            running = false;
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}*/

void runServer() {
    PVIGEM_CLIENT controller_client = vigem_alloc();
    vigem_connect(controller_client);

    WSADATA wsaData;
    int iResult;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return;
    }

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo* result = NULL, hints{};

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    iResult = getaddrinfo(NULL, "4000", &hints, &result);
    if (iResult != 0) {
        std::cerr << "getaddrinfo failed: " << iResult << std::endl;
        WSACleanup();
        return;
    }

    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    std::cout << "Server listening on port 4000..." << std::endl;

    ClientSocket = accept(ListenSocket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) {
        std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    int timeout = 20000;
    setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    closesocket(ListenSocket);

    std::cout << "Client connected. Assuming WebSocket handshake completed." << std::endl;

    running = true;
    lastPingTime = std::chrono::steady_clock::now();
    //std::thread timerThread(watchdog);

    const int DEFAULT_BUFLEN = 512;
    std::vector<char> recvbuf(DEFAULT_BUFLEN);

    while (running) {
        int iResult = recv(ClientSocket, recvbuf.data(), DEFAULT_BUFLEN, 0);
        if (iResult > 0) {
            std::string csv(recvbuf.data(), iResult);
            if (csv.length() > 10 && !(csv.find("Sec-WebSocket-Key") != std::string::npos) && !(csv.find("ping") != std::string::npos)) {
                X360Packet* packet = reinterpret_cast<X360Packet*>(recvbuf.data());

                auto swap16 = [](SHORT x) -> SHORT {
                    return (x << 8) | ((x >> 8) & 0xFF);
                    };

                DWORD controllerIndex = SwapDWORD(packet->userIndex);

                if (runningPads[controllerIndex] == nullptr) {
                    PVIGEM_TARGET pad = vigem_target_x360_alloc();
                    runningPads[controllerIndex] = pad;
                    vigem_target_add(controller_client, runningPads[controllerIndex]);
                    vigem_target_x360_register_notification(controller_client, pad, &notification, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(ClientSocket)));
                }

                report.wButtons = swap16(packet->wButtons);
                report.bLeftTrigger = packet->bLeftTrigger;
                report.bRightTrigger = packet->bRightTrigger;
                report.sThumbLX = swap16(packet->sThumbLX);
                report.sThumbLY = swap16(packet->sThumbLY);
                report.sThumbRX = swap16(packet->sThumbRX);
                report.sThumbRY = swap16(packet->sThumbRY);

                vigem_target_x360_update(controller_client, runningPads[controllerIndex], report);
            }
            if ((csv.find("ping") != std::string::npos)) {
                lastPingTime = std::chrono::steady_clock::now();
            }
        }
        else {
            std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
            running = false;
        }
    }

    for (int i = 0; i < 4; i++)
    {
        if (runningPads[i] != nullptr) {
            vigem_target_x360_unregister_notification(runningPads[i]);
            vigem_target_remove(controller_client, runningPads[i]);
            vigem_target_free(runningPads[i]);
            runningPads[i] = nullptr;
        }
    }
    vigem_disconnect(controller_client);
    vigem_free(controller_client);

    closesocket(ClientSocket);
    WSACleanup();
    //timerThread.join();
}

#ifdef _DEBUG
int main() {
#else
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd) {
#endif
    while (true) {
        runServer();
        std::cout << "Restarting logic in 5 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}