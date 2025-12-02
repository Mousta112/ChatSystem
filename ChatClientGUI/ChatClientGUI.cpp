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

// Styling globals
HFONT  g_hFont = nullptr;
HBRUSH g_bgBrush = nullptr; // خلفية عامة
HBRUSH g_offWhiteBrush = nullptr; // chat + input + buttons
BYTE   g_alpha = 0;       // للـ fade-in

// Network
SOCKET sock;
bool connected = false;

// Username state
bool hasUsername = false;
wstring g_username;

// Custom Windows Message for new incoming text
#define WM_NEW_MESSAGE (WM_APP + 1)
#define ID_TIMER_FADE  1001

// Append text to chat edit + سطر جديد
void AppendText(HWND hEdit, const wstring& text, bool addNewLine = true)
{
    wstring t = text;
    if (addNewLine) t += L"\r\n";

    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
}

// دالة عامة للديسكونكت
void DoDisconnect()
{
    if (!connected) return;

    connected = false;
    closesocket(sock);
    WSACleanup();

    EnableWindow(hSendButton, FALSE);
    EnableWindow(hDisconnectButton, FALSE);

    AppendText(hChatLog, L"[*] You disconnected.");
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
            // السيرفر قفل أو حصل Error
            connected = false;
            AppendText(hChatLog, L"[*] Connection closed by server.");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            break;
        }

        string msg(buf, bytesReceived);

        // نحول من UTF-8 أو ASCII لـ wide string
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
        // ---------- Styling objects ----------
        // خلفية فاتحة عامة
        g_bgBrush = CreateSolidBrush(RGB(240, 242, 246));
        // off-white ناعم chat + input + buttons
        g_offWhiteBrush = CreateSolidBrush(RGB(242, 238, 232));

        // Font حديث
        g_hFont = CreateFontW(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );

        // Chat log
        hChatLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
            15, 15, 550, 225,
            hwnd, (HMENU)1, GetModuleHandle(nullptr), nullptr);

        // Input box
        hInput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            15, 250, 360, 28,
            hwnd, (HMENU)2, GetModuleHandle(nullptr), nullptr);

        // Send button
        hSendButton = CreateWindowExW(
            0, L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            385, 250, 80, 28,
            hwnd, (HMENU)3, GetModuleHandle(nullptr), nullptr);

        // Disconnect button
        hDisconnectButton = CreateWindowExW(
            0, L"BUTTON", L"Disconnect",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            475, 250, 90, 28,
            hwnd, (HMENU)4, GetModuleHandle(nullptr), nullptr);

        // Apply font
        SendMessageW(hChatLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hInput, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hSendButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hDisconnectButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Rounded buttons باستخدام regions
        HRGN rgnSend = CreateRoundRectRgn(0, 0, 80, 28, 10, 10);
        SetWindowRgn(hSendButton, rgnSend, TRUE);
        HRGN rgnDisc = CreateRoundRectRgn(0, 0, 90, 28, 10, 10);
        SetWindowRgn(hDisconnectButton, rgnDisc, TRUE);

        // -------- Connect to server --------
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            AppendText(hChatLog, L"[WSAStartup failed]");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            break;
        }

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET)
        {
            AppendText(hChatLog, L"[Socket creation failed]");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            break;
        }

        sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(54000);
        inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

        if (connect(sock, (sockaddr*)&server, sizeof(server)) == 0)
        {
            connected = true;

            AppendText(hChatLog, L"[Connected to server]");
            AppendText(hChatLog, L"----------------------------------------");
            AppendText(hChatLog, L"[Enter your username then press Send]");

            // تشغيل Thread الاستقبال
            thread(ReceiverThread, hwnd).detach();
        }
        else
        {
            AppendText(hChatLog, L"[Failed to connect to server]");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
        }

        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);

        // زرار Send
        if (id == 3 && connected)
        {
            wchar_t buffer[512];
            GetWindowTextW(hInput, buffer, 512);

            if (wcslen(buffer) > 0)
            {
                wstring wmsg(buffer);

                // أول Send = Username
                if (!hasUsername)
                {
                    g_username = wmsg;
                    hasUsername = true;

                    // حوّل الـ username لـ UTF-8 وابعته للسيرفر
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, g_username.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    string msg(size_needed - 1, 0); // بدون الـ null terminator
                    WideCharToMultiByte(CP_UTF8, 0, g_username.c_str(), -1, &msg[0], size_needed - 1, nullptr, nullptr);

                    send(sock, msg.c_str(), (int)msg.size(), 0);

                    AppendText(hChatLog, L"----------------------------------------");
                    AppendText(hChatLog, L"[*] You joined as: " + g_username);

                    wstring title = L"Chat Client (GUI) - " + g_username;
                    SetWindowTextW(hwnd, title.c_str());

                    SetWindowTextW(hInput, L"");
                }
                else
                {
                    // إرسال رسالة شات عادية
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    string msg(size_needed - 1, 0);
                    WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, &msg[0], size_needed - 1, nullptr, nullptr);

                    send(sock, msg.c_str(), (int)msg.size(), 0);

                    wstring line = L"You (" + g_username + L"): " + wmsg;
                    AppendText(hChatLog, line);

                    SetWindowTextW(hInput, L"");
                }
            }
        }
        // زرار Disconnect
        else if (id == 4)
        {
            DoDisconnect();
        }

        break;
    }

    case WM_NEW_MESSAGE:
    {
        wchar_t* text = (wchar_t*)lParam;
        wstring line = text;
        AppendText(hChatLog, line);
        delete[] text;
        break;
    }

    // رسم خلفية الويندو (لون هادي)
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_bgBrush ? g_bgBrush : (HBRUSH)(COLOR_WINDOW + 1));
        return 1;
    }

    // ألوان الـ Edit controls (chat + input)
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;

        SetTextColor(hdc, RGB(30, 41, 59)); // نص غامق شوية
        SetBkColor(hdc, RGB(242, 238, 232));// off-white

        return (INT_PTR)g_offWhiteBrush;
    }

    // ألوان الأزرار
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        // كله off-white
        SetTextColor(hdc, RGB(30, 41, 59));
        SetBkColor(hdc, RGB(242, 238, 232));

        return (INT_PTR)g_offWhiteBrush;
    }

    // Fade-in animation
    case WM_TIMER:
    {
        if (wParam == ID_TIMER_FADE)
        {
            if (g_alpha < 255)
            {
                g_alpha = (BYTE)min(255, g_alpha + 20);
                SetLayeredWindowAttributes(hwnd, 0, g_alpha, LWA_ALPHA);
            }
            else
            {
                KillTimer(hwnd, ID_TIMER_FADE);
            }
        }
        break;
    }

    case WM_DESTROY:
        DoDisconnect();

        if (g_hFont) DeleteObject(g_hFont);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_offWhiteBrush) DeleteObject(g_offWhiteBrush);

        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Entry point
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
        620, 340,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    // إعداد الـ Fade-in
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    g_alpha = 0;
    SetLayeredWindowAttributes(hwnd, 0, g_alpha, LWA_ALPHA);
    SetTimer(hwnd, ID_TIMER_FADE, 20, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
