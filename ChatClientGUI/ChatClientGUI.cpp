#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <thread>
#include <cwchar>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

// GUI Controls
HWND hChatLog;
HWND hInput;
HWND hSendButton;
HWND hDisconnectButton;

HWND hIpEdit;
HWND hPortEdit;
HWND hConnectButton;

// Network
SOCKET sock;
bool connected = false;

// Username state
bool hasUsername = false;
wstring g_username;

// Custom Windows Message for new incoming text
#define WM_NEW_MESSAGE (WM_APP + 1)

// Append text to chat edit
void AppendText(HWND hEdit, const wstring& text)
{
    wstring t = text + L"\r\n";
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
}

void DoDisconnect()
{
    if (!connected) return;

    connected = false;
    closesocket(sock);
    WSACleanup();

    AppendText(hChatLog, L"[*] Disconnected.");

    EnableWindow(hSendButton, FALSE);
    EnableWindow(hDisconnectButton, FALSE);
    EnableWindow(hConnectButton, TRUE);
}

// Thread: receive messages from server
void ReceiverThread(HWND hwnd)
{
    char buf[4096];

    while (connected)
    {
        ZeroMemory(buf, 4096);
        int bytesReceived = recv(sock, buf, 4096, 0);

        if (bytesReceived <= 0)
        {
            AppendText(hChatLog, L"[*] Connection closed by server.");
            DoDisconnect();
            break;
        }

        string msg(buf, bytesReceived);

        int wsize = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), bytesReceived, nullptr, 0);
        wstring wmsg(wsize, 0);
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), bytesReceived, &wmsg[0], wsize);

        wchar_t* copy = new wchar_t[wmsg.size() + 1];
        wcscpy_s(copy, wmsg.size() + 1, wmsg.c_str());

        PostMessage(hwnd, WM_NEW_MESSAGE, 0, (LPARAM)copy);
    }
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // ---- IP ----
        CreateWindowW(L"STATIC", L"IP:",
            WS_CHILD | WS_VISIBLE, 10, 10, 20, 20,
            hwnd, nullptr, nullptr, nullptr);

        hIpEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            35, 8, 130, 22,
            hwnd, (HMENU)10, nullptr, nullptr);

        // ---- PORT ----
        CreateWindowW(L"STATIC", L"Port:",
            WS_CHILD | WS_VISIBLE, 180, 10, 40, 20,
            hwnd, nullptr, nullptr, nullptr);

        hPortEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"54000",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            220, 8, 70, 22,
            hwnd, (HMENU)11, nullptr, nullptr);

        // ---- CONNECT ----
        hConnectButton = CreateWindowW(
            L"BUTTON", L"Connect",
            WS_CHILD | WS_VISIBLE,
            300, 8, 80, 22,
            hwnd, (HMENU)12, nullptr, nullptr);

        // Chat log
        hChatLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
            10, 40, 560, 210,
            hwnd, (HMENU)1, nullptr, nullptr);

        // Input box
        hInput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            10, 260, 370, 25,
            hwnd, (HMENU)2, nullptr, nullptr);

        // Send button
        hSendButton = CreateWindowW(
            L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE,
            390, 260, 80, 25,
            hwnd, (HMENU)3, nullptr, nullptr);

        // Disconnect button
        hDisconnectButton = CreateWindowW(
            L"BUTTON", L"Disconnect",
            WS_CHILD | WS_VISIBLE,
            480, 260, 90, 25,
            hwnd, (HMENU)4, nullptr, nullptr);

        EnableWindow(hSendButton, FALSE);
        EnableWindow(hDisconnectButton, FALSE);

        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);

        // CONNECT BUTTON
        if (id == 12)
        {
            wchar_t ipBuf[64], portBuf[16];
            GetWindowTextW(hIpEdit, ipBuf, 64);
            GetWindowTextW(hPortEdit, portBuf, 16);

            string ip = string(ipBuf, ipBuf + wcslen(ipBuf));
            int port = _wtoi(portBuf);

            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);

            sock = socket(AF_INET, SOCK_STREAM, 0);

            sockaddr_in server;
            server.sin_family = AF_INET;
            server.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &server.sin_addr);

            if (connect(sock, (sockaddr*)&server, sizeof(server)) == 0)
            {
                connected = true;

                AppendText(hChatLog, L"[Connected to server]");
                AppendText(hChatLog, L"[Enter username then press Send]");

                EnableWindow(hSendButton, TRUE);
                EnableWindow(hDisconnectButton, TRUE);
                EnableWindow(hConnectButton, FALSE);

                thread(ReceiverThread, hwnd).detach();
            }
            else
            {
                AppendText(hChatLog, L"[Failed to connect]");
            }
        }

        // SEND BUTTON
        else if (id == 3 && connected)
        {
            wchar_t buffer[512];
            GetWindowTextW(hInput, buffer, 512);

            if (wcslen(buffer) == 0) break;
            wstring wmsg(buffer);

            if (!hasUsername)
            {
                g_username = wmsg;
                hasUsername = true;

                int size_needed = WideCharToMultiByte(CP_UTF8, 0, g_username.c_str(), -1, nullptr, 0, nullptr, nullptr);
                string msg(size_needed - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, g_username.c_str(), -1, &msg[0], size_needed - 1, nullptr, nullptr);

                send(sock, msg.c_str(), (int)msg.size(), 0);

                AppendText(hChatLog, L"[*] Joined as: " + g_username);

                wstring title = L"Chat Client - " + g_username;
                SetWindowTextW(hwnd, title.c_str());
            }
            else
            {
                int needed = WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
                string msg(needed - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, &msg[0], needed - 1, nullptr, nullptr);

                send(sock, msg.c_str(), (int)msg.size(), 0);

                AppendText(hChatLog, L"You (" + g_username + L"): " + wmsg);
            }

            SetWindowTextW(hInput, L"");
        }

        // DISCONNECT BUTTON
        else if (id == 4)
        {
            DoDisconnect();
        }

        break;
    }

    case WM_NEW_MESSAGE:
    {
        wchar_t* text = (wchar_t*)lParam;
        wstring msg = text;
        AppendText(hChatLog, msg);
        delete[] text;
        break;
    }

    case WM_DESTROY:
        DoDisconnect();
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"ChatClientGUIClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Chat Client (GUI)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        600, 340,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
