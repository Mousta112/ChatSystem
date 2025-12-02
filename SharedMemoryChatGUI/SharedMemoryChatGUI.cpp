#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <thread>
#include <vector>
#include <cstring>

using namespace std;

// ===== Shared Memory + Sync =====
const wchar_t* SHM_NAME = L"Local\\ChatSharedMemory_OS_Project_GUI";
const wchar_t* SEM_MUTEX_NAME = L"Local\\Chat_SharedMutex_GUI";

const int MSG_SIZE = 256;
const int MAX_MESSAGES = 1024;

struct ChatMessage
{
    char sender[32];
    char text[MSG_SIZE];
};

struct SharedData
{
    ChatMessage messages[MAX_MESSAGES];
    int messageCount;
};

// ===== GUI Globals =====
HWND   hChatLog;
HWND   hInput;
HWND   hSendButton;
HWND   hDisconnectButton;

HFONT  g_hFont = nullptr;
HBRUSH g_bgBrush = nullptr; // خلفية عامة
HBRUSH g_offWhiteBrush = nullptr; // chat + input + buttons
BYTE   g_alpha = 0;

HANDLE g_hMapFile = nullptr;
HANDLE g_hMutex = nullptr;
SharedData* g_shared = nullptr;

bool   g_running = true;
bool   g_connected = false;
bool   g_hasUsername = false;
string g_username;           // narrow
wstring g_usernameW;         // wide

#define WM_NEW_MESSAGE (WM_APP + 1)
#define ID_TIMER_FADE  1001

// append text to chat
void AppendText(HWND hEdit, const wstring& text, bool addNewLine = true)
{
    wstring t = text;
    if (addNewLine) t += L"\r\n";

    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
}

void Cleanup()
{
    g_running = false;

    EnableWindow(hSendButton, FALSE);
    EnableWindow(hDisconnectButton, FALSE);

    if (g_shared)
    {
        UnmapViewOfFile(g_shared);
        g_shared = nullptr;
    }
    if (g_hMapFile)
    {
        CloseHandle(g_hMapFile);
        g_hMapFile = nullptr;
    }
    if (g_hMutex)
    {
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
    }
    g_connected = false;
}

// disconnect + رسالة بسيطة
void DoDisconnect()
{
    if (!g_connected) return;

    // نبعث رسالة "left" في الشات
    if (g_hasUsername && g_shared && g_hMutex)
    {
        string leftMsg = g_username + " left the chat.";

        WaitForSingleObject(g_hMutex, INFINITE);
        int idx = g_shared->messageCount % MAX_MESSAGES;
        strncpy_s(g_shared->messages[idx].sender, g_username.c_str(), _TRUNCATE);
        strncpy_s(g_shared->messages[idx].text, leftMsg.c_str(), _TRUNCATE);
        g_shared->messageCount++;
        ReleaseSemaphore(g_hMutex, 1, nullptr);
    }

    AppendText(hChatLog, L"[*] You disconnected.");
    Cleanup();
}

// thread استقبال
void ReceiverThread(HWND hwnd)
{
    int lastSeen = 0;

    while (g_running && g_shared && g_hMutex)
    {
        vector<ChatMessage> newMessages;

        DWORD res = WaitForSingleObject(g_hMutex, INFINITE);
        if (res != WAIT_OBJECT_0)
            break;

        int total = g_shared->messageCount;
        for (int i = lastSeen; i < total; ++i)
        {
            newMessages.push_back(g_shared->messages[i]);
        }
        lastSeen = total;

        ReleaseSemaphore(g_hMutex, 1, nullptr);

        for (auto& msg : newMessages)
        {
            // تجاهل رسائلك انت
            if (g_hasUsername && strcmp(msg.sender, g_username.c_str()) == 0)
                continue;

            string sSender = msg.sender;
            string sText = msg.text;

            wstring wsSender(sSender.begin(), sSender.end());
            wstring wsText(sText.begin(), sText.end());
            wstring line = L"[" + wsSender + L"]: " + wsText;

            wchar_t* copy = new wchar_t[line.size() + 1];
            wcscpy_s(copy, line.size() + 1, line.c_str());

            PostMessageW(hwnd, WM_NEW_MESSAGE, 0, (LPARAM)copy);
        }

        Sleep(100);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static thread receiverThread;

    switch (msg)
    {
    case WM_CREATE:
    {
        // ========= Styling =========
        g_bgBrush = CreateSolidBrush(RGB(240, 242, 246));
        g_offWhiteBrush = CreateSolidBrush(RGB(242, 238, 232));

        g_hFont = CreateFontW(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );

        // ========= Controls =========
        hChatLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
            15, 15, 550, 225,
            hwnd, (HMENU)1, GetModuleHandle(nullptr), nullptr);

        hInput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            15, 250, 360, 28,
            hwnd, (HMENU)2, GetModuleHandle(nullptr), nullptr);

        hSendButton = CreateWindowExW(
            0, L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            385, 250, 80, 28,
            hwnd, (HMENU)3, GetModuleHandle(nullptr), nullptr);

        hDisconnectButton = CreateWindowExW(
            0, L"BUTTON", L"Disconnect",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            475, 250, 90, 28,
            hwnd, (HMENU)4, GetModuleHandle(nullptr), nullptr);

        SendMessageW(hChatLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hInput, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hSendButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hDisconnectButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Rounded buttons
        {
            HRGN rgnSend = CreateRoundRectRgn(0, 0, 80, 28, 10, 10);
            SetWindowRgn(hSendButton, rgnSend, TRUE);
            HRGN rgnDisc = CreateRoundRectRgn(0, 0, 90, 28, 10, 10);
            SetWindowRgn(hDisconnectButton, rgnDisc, TRUE);
        }

        // ========= Shared Memory + Mutex =========
        g_hMapFile = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(SharedData),
            SHM_NAME
        );

        if (!g_hMapFile)
        {
            AppendText(hChatLog, L"[Failed to create/open shared memory]");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            break;
        }

        bool firstCreator = (GetLastError() != ERROR_ALREADY_EXISTS);

        g_shared = (SharedData*)MapViewOfFile(
            g_hMapFile,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            sizeof(SharedData)
        );

        if (!g_shared)
        {
            AppendText(hChatLog, L"[Failed to map shared memory]");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            CloseHandle(g_hMapFile);
            g_hMapFile = nullptr;
            break;
        }

        if (firstCreator)
        {
            ZeroMemory(g_shared, sizeof(SharedData));
            g_shared->messageCount = 0;
        }

        g_hMutex = CreateSemaphoreW(
            nullptr,
            1,  // initial count = 1
            1,  // max count = 1 (mutex)
            SEM_MUTEX_NAME
        );

        if (!g_hMutex)
        {
            AppendText(hChatLog, L"[Failed to create/open mutex semaphore]");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            Cleanup();
            break;
        }

        g_connected = true;
        g_running = true;

        AppendText(hChatLog, L"[Connected to Shared Memory Chat]");
        AppendText(hChatLog, L"----------------------------------------");
        AppendText(hChatLog, L"[Enter your username then press Send]");

        // تشغيل thread الاستقبال
        receiverThread = thread(ReceiverThread, hwnd);

        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);

        if (id == 3 && g_connected) // Send
        {
            wchar_t buffer[512];
            GetWindowTextW(hInput, buffer, 512);

            if (wcslen(buffer) == 0)
                break;

            wstring wmsg(buffer);
            string msgNarrow(wmsg.begin(), wmsg.end());

            // أول Send = Username
            if (!g_hasUsername)
            {
                g_username = msgNarrow;
                g_usernameW = wmsg;
                if (g_username.empty())
                    g_username = "User";

                // system message: joined
                string joined = g_username + " joined the chat.";

                WaitForSingleObject(g_hMutex, INFINITE);
                int idx = g_shared->messageCount % MAX_MESSAGES;
                strncpy_s(g_shared->messages[idx].sender, g_username.c_str(), _TRUNCATE);
                strncpy_s(g_shared->messages[idx].text, joined.c_str(), _TRUNCATE);
                g_shared->messageCount++;
                ReleaseSemaphore(g_hMutex, 1, nullptr);

                g_hasUsername = true;

                AppendText(hChatLog, L"----------------------------------------");
                AppendText(hChatLog, L"[*] You joined as: " + g_usernameW);

                wstring title = L"Shared Memory Chat (GUI) - " + g_usernameW;
                SetWindowTextW(hwnd, title.c_str());

                SetWindowTextW(hInput, L"");
            }
            else
            {
                // كتابة الرسالة في shared memory
                WaitForSingleObject(g_hMutex, INFINITE);
                int idx = g_shared->messageCount % MAX_MESSAGES;
                strncpy_s(g_shared->messages[idx].sender, g_username.c_str(), _TRUNCATE);
                strncpy_s(g_shared->messages[idx].text, msgNarrow.c_str(), _TRUNCATE);
                g_shared->messageCount++;
                ReleaseSemaphore(g_hMutex, 1, nullptr);

                // عرض الرسالة محلياً مرة واحدة فقط
                wstring line = L"You (" + g_usernameW + L"): " + wmsg;
                AppendText(hChatLog, line);

                SetWindowTextW(hInput, L"");
            }
        }
        else if (id == 4) // Disconnect
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

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_bgBrush ? g_bgBrush : (HBRUSH)(COLOR_WINDOW + 1));
        return 1;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(30, 41, 59));
        SetBkColor(hdc, RGB(242, 238, 232)); // off-white
        return (INT_PTR)g_offWhiteBrush;
    }

    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(30, 41, 59));
        SetBkColor(hdc, RGB(242, 238, 232));
        return (INT_PTR)g_offWhiteBrush;
    }

    case WM_TIMER:
    {
        if (wParam == ID_TIMER_FADE)
        {
            if (g_alpha < 255)
            {
                g_alpha = (BYTE)min(255, (int)g_alpha + 20);
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
    {
        DoDisconnect();

        g_running = false;

        // ننتظر الـ receiver thread لو شغال
        if (receiverThread.joinable())
            receiverThread.join();

        if (g_hFont) DeleteObject(g_hFont);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_offWhiteBrush) DeleteObject(g_offWhiteBrush);

        PostQuitMessage(0);
        break;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"SharedMemoryChatGUIClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Shared Memory Chat (GUI)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        620, 340,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    // Fade-in
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    g_alpha = 0;
    SetLayeredWindowAttributes(hwnd, 0, g_alpha, LWA_ALPHA);
    SetTimer(hwnd, ID_TIMER_FADE, 20, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
