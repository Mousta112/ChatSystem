#define WIN32_LEAN_AND_MEAN
#define BTN_ANIM_TIMER  101

#ifndef EM_HIDESELECTION
#define EM_HIDESELECTION (WM_USER + 63)
#endif

#include <windows.h>
#include <commctrl.h>
#include <Richedit.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

using namespace std;

// ===================== Shared Memory + Sync =====================
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

// ===================== GUI Globals =====================
HWND hChatLog = nullptr;              // RichEdit
HWND hInput = nullptr;
HWND hSendButton = nullptr;
HWND hDisconnectButton = nullptr;
HWND hThemeButton = nullptr;          // 🌙/☀

HWND hInputPlaceholder = nullptr;

static HWND hStatusLabel = nullptr;
static HWND hStatusValue = nullptr;

static HFONT g_font = nullptr;
static HFONT g_emojiFont = nullptr;
static HFONT g_placeholderFont = nullptr;

static HBRUSH g_brBg = nullptr;
static HBRUSH g_brEditBg = nullptr;

static HWND   g_tooltip = nullptr;
static WNDPROC g_oldInputProc = nullptr;
static HWND g_hwndMain = nullptr;

static BYTE g_alpha = 0;

// ===================== Theme =====================
static bool g_darkMode = false;

// Light
static COLORREF CLR_BG = RGB(255, 255, 255);
static COLORREF CLR_EDIT_BG = RGB(255, 255, 255);
static COLORREF CLR_TEXT = RGB(30, 30, 30);
static COLORREF CLR_HINT = RGB(140, 140, 140);

static COLORREF CLR_SYS = RGB(100, 116, 139);
static COLORREF CLR_OTHER = RGB(37, 99, 235);
static COLORREF CLR_YOU = RGB(22, 163, 74);

// Dark
static COLORREF CLR_BG_DARK = RGB(18, 18, 18);
static COLORREF CLR_EDIT_BG_DARK = RGB(28, 28, 28);
static COLORREF CLR_TEXT_DARK = RGB(235, 235, 235);
static COLORREF CLR_HINT_DARK = RGB(160, 160, 160);

static COLORREF CLR_SYS_DARK = RGB(148, 163, 184);
static COLORREF CLR_OTHER_DARK = RGB(96, 165, 250);
static COLORREF CLR_YOU_DARK = RGB(74, 222, 128);

static const wchar_t* INPUT_PLACEHOLDER = L"Type a message...";

// ===================== Shared handles =====================
HANDLE g_hMapFile = nullptr;
HANDLE g_hMutex = nullptr;  // Semaphore as mutex (same as your logic)
SharedData* g_shared = nullptr;

bool g_running = true;
bool g_connected = false;

bool g_hasUsername = false;
string  g_username;    // narrow (same as your code)
wstring g_usernameW;

#define WM_NEW_MESSAGE (WM_APP + 1)
#define ID_TIMER_FADE  1001

// ===================== Fancy Buttons =====================
enum class BtnKind { Primary, Secondary, Danger, Success };

struct BtnState
{
    HWND hwnd{};
    BtnKind kind{ BtnKind::Primary };
    bool hovered{ false };
    bool pressed{ false };
    int anim{ 0 };
    int animTarget{ 0 };
};

static BtnState g_btnSend{};
static BtnState g_btnDisconnect{};
static BtnState g_btnTheme{};

static COLORREF Adjust(COLORREF c, int delta)
{
    int r = (int)GetRValue(c) + delta;
    int g = (int)GetGValue(c) + delta;
    int b = (int)GetBValue(c) + delta;
    r = (r < 0) ? 0 : (r > 255 ? 255 : r);
    g = (g < 0) ? 0 : (g > 255 ? 255 : g);
    b = (b < 0) ? 0 : (b > 255 ? 255 : b);
    return RGB(r, g, b);
}

static BtnState* GetBtnState(HWND h)
{
    if (g_btnSend.hwnd == h) return &g_btnSend;
    if (g_btnDisconnect.hwnd == h) return &g_btnDisconnect;
    if (g_btnTheme.hwnd == h) return &g_btnTheme;
    return nullptr;
}

static void RegisterFancyButton(HWND hwndBtn, BtnKind kind)
{
    BtnState* st = GetBtnState(hwndBtn);
    if (!st) return;

    st->kind = kind;
    st->hovered = false;
    st->pressed = false;
    st->anim = 0;
    st->animTarget = 0;
}

LRESULT CALLBACK FancyBtnProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BtnState* st = GetBtnState(hWnd);

    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (st)
        {
            if (!st->hovered)
            {
                st->hovered = true;
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hWnd;
                TrackMouseEvent(&tme);
            }
            st->animTarget = 100;
            SetTimer(hWnd, BTN_ANIM_TIMER, 15, nullptr);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;

    case WM_MOUSELEAVE:
        if (st)
        {
            st->hovered = false;
            st->pressed = false;
            st->animTarget = 0;
            SetTimer(hWnd, BTN_ANIM_TIMER, 15, nullptr);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;

    case WM_TIMER:
        if (wParam == BTN_ANIM_TIMER && st)
        {
            int step = 12;
            if (st->anim < st->animTarget) st->anim = (std::min)(100, st->anim + step);
            else if (st->anim > st->animTarget) st->anim = (std::max)(0, st->anim - step);

            InvalidateRect(hWnd, nullptr, TRUE);

            if (st->anim == st->animTarget)
                KillTimer(hWnd, BTN_ANIM_TIMER);
        }
        break;

    case WM_LBUTTONDOWN:
        if (st) { st->pressed = true; InvalidateRect(hWnd, nullptr, TRUE); }
        break;

    case WM_LBUTTONUP:
        if (st) { st->pressed = false; InvalidateRect(hWnd, nullptr, TRUE); }
        break;

    case WM_ENABLE:
        InvalidateRect(hWnd, nullptr, TRUE);
        break;
    }

    WNDPROC old = (WNDPROC)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (!old) return DefWindowProcW(hWnd, msg, wParam, lParam);
    return CallWindowProcW(old, hWnd, msg, wParam, lParam);
}

static void SubclassButton(HWND hBtn)
{
    WNDPROC oldProc = (WNDPROC)SetWindowLongPtrW(hBtn, GWLP_WNDPROC, (LONG_PTR)FancyBtnProc);
    SetWindowLongPtrW(hBtn, GWLP_USERDATA, (LONG_PTR)oldProc);
}

static void DrawFancyButton(const DRAWITEMSTRUCT* dis, const BtnState* st)
{
    RECT rc = dis->rcItem;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    HDC hdc = dis->hDC;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT r0{ 0,0,w,h };

    HBRUSH brBack = CreateSolidBrush(CLR_BG);
    FillRect(mem, &r0, brBack);
    DeleteObject(brBack);

    bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;

    COLORREF baseFill{};
    switch (st ? st->kind : BtnKind::Primary)
    {
    case BtnKind::Primary:   baseFill = RGB(37, 99, 235);   break;
    case BtnKind::Secondary: baseFill = RGB(100, 116, 139); break;
    case BtnKind::Danger:    baseFill = RGB(220, 38, 38);   break;
    case BtnKind::Success:   baseFill = RGB(22, 163, 74);   break;
    }

    COLORREF fill = baseFill;
    COLORREF textClr = RGB(255, 255, 255);

    if (!enabled)
    {
        fill = RGB(210, 210, 210);
        textClr = RGB(120, 120, 120);
    }
    else if (st)
    {
        int hoverBoost = (st->anim * 18) / 100;
        if (st->pressed) fill = Adjust(baseFill, -25);
        else if (st->hovered) fill = Adjust(baseFill, +hoverBoost);
    }

    int radius = 12;
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    HBRUSH brFill = CreateSolidBrush(fill);
    FillRgn(mem, rgn, brFill);

    HPEN pen = CreatePen(PS_SOLID, 1, Adjust(fill, -35));
    HGDIOBJ oldPen = SelectObject(mem, pen);
    HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(HOLLOW_BRUSH));
    RoundRect(mem, 0, 0, w, h, radius, radius);

    SelectObject(mem, oldBrush);
    SelectObject(mem, oldPen);
    DeleteObject(pen);

    DeleteObject(brFill);
    DeleteObject(rgn);

    wchar_t text[128]{};
    GetWindowTextW(dis->hwndItem, text, 128);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, textClr);

    HFONT useFont = g_font ? g_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (dis->hwndItem == hThemeButton && g_emojiFont)
        useFont = g_emojiFont;

    HGDIOBJ oldFont = SelectObject(mem, useFont);

    RECT tr = r0;
    DrawTextW(mem, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem, oldFont);

    BitBlt(hdc, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ===================== UI Helpers =====================
static wstring NowTimeStamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32]{};
    wsprintfW(buf, L"[%02d:%02d] ", st.wHour, st.wMinute);
    return buf;
}

static void TrimCRLF(wstring& s)
{
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n'))
        s.pop_back();
}

static wstring MakeSingleLine(wstring s)
{
    for (auto& ch : s)
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    return s;
}

static void UpdateStatusUI(const wchar_t* text)
{
    if (hStatusValue) SetWindowTextW(hStatusValue, text);
}

static void UpdateInputPlaceholderVisibility()
{
    if (!hInputPlaceholder || !hInput) return;
    int len = GetWindowTextLengthW(hInput);
    bool show = (len == 0) && (GetFocus() != hInput);
    ShowWindow(hInputPlaceholder, show ? SW_SHOW : SW_HIDE);
}

static void RecreateBrushes()
{
    if (g_brBg) { DeleteObject(g_brBg); g_brBg = nullptr; }
    if (g_brEditBg) { DeleteObject(g_brEditBg); g_brEditBg = nullptr; }
    g_brBg = CreateSolidBrush(CLR_BG);
    g_brEditBg = CreateSolidBrush(CLR_EDIT_BG);
}

static void UpdateThemeIcon()
{
    if (!hThemeButton) return;
    SetWindowTextW(hThemeButton, g_darkMode ? L"☀" : L"🌙");
}

static void ApplyTheme(bool dark)
{
    g_darkMode = dark;

    if (!g_darkMode)
    {
        CLR_BG = RGB(255, 255, 255);
        CLR_EDIT_BG = RGB(255, 255, 255);
        CLR_TEXT = RGB(30, 30, 30);
        CLR_HINT = RGB(140, 140, 140);

        CLR_SYS = RGB(100, 116, 139);
        CLR_OTHER = RGB(37, 99, 235);
        CLR_YOU = RGB(22, 163, 74);
    }
    else
    {
        CLR_BG = CLR_BG_DARK;
        CLR_EDIT_BG = CLR_EDIT_BG_DARK;
        CLR_TEXT = CLR_TEXT_DARK;
        CLR_HINT = CLR_HINT_DARK;

        CLR_SYS = CLR_SYS_DARK;
        CLR_OTHER = CLR_OTHER_DARK;
        CLR_YOU = CLR_YOU_DARK;
    }

    RecreateBrushes();

    if (hChatLog)
    {
        SendMessageW(hChatLog, EM_SETBKGNDCOLOR, 0, (LPARAM)CLR_EDIT_BG);

        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = CLR_TEXT;
        SendMessageW(hChatLog, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    }

    UpdateThemeIcon();

    if (g_hwndMain)
    {
        InvalidateRect(g_hwndMain, nullptr, TRUE);
        HWND kids[] = {
            hChatLog, hInput, hStatusLabel, hStatusValue,
            hInputPlaceholder, hSendButton, hDisconnectButton, hThemeButton
        };
        for (HWND k : kids) if (k) InvalidateRect(k, nullptr, TRUE);
        UpdateInputPlaceholderVisibility();
    }
}

static void AddToolTip(HWND tooltip, HWND target, LPCWSTR text)
{
    if (!tooltip || !target) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = GetParent(target);
    ti.uId = (UINT_PTR)target;
    ti.lpszText = const_cast<LPWSTR>(text);
    GetClientRect(target, &ti.rect);
    SendMessageW(tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

// ===================== RichEdit Append =====================
static void RichAppendWithFormat(HWND hRe, const wstring& text, COLORREF color, bool bold)
{
    if (!hRe) return;

    int len = GetWindowTextLengthW(hRe);
    SendMessageW(hRe, EM_SETSEL, len, len);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.crTextColor = color;
    cf.dwEffects = 0;
    if (bold) cf.dwEffects |= CFE_BOLD;

    SendMessageW(hRe, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hRe, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());

    int newLen = GetWindowTextLengthW(hRe);
    SendMessageW(hRe, EM_SETSEL, newLen, newLen);
    SendMessageW(hRe, EM_SCROLLCARET, 0, 0);
}

enum class LineKind { System, You, Other };

static void AppendRichLine(LineKind kind, const wstring& who, const wstring& msg)
{
    wstring ts = NowTimeStamp();
    wstring nl = L"\r\n";

    COLORREF nameClr = CLR_SYS;
    COLORREF msgClr = CLR_SYS;

    if (kind == LineKind::Other) { nameClr = CLR_OTHER; msgClr = CLR_TEXT; }
    if (kind == LineKind::You) { nameClr = CLR_YOU;   msgClr = CLR_TEXT; }
    if (kind == LineKind::System) { nameClr = CLR_SYS;   msgClr = CLR_SYS; }

    RichAppendWithFormat(hChatLog, ts, CLR_HINT, false);

    if (!who.empty())
    {
        RichAppendWithFormat(hChatLog, who, nameClr, true);
        RichAppendWithFormat(hChatLog, L": ", CLR_HINT, false);
    }

    RichAppendWithFormat(hChatLog, msg, msgClr, false);
    RichAppendWithFormat(hChatLog, nl, CLR_HINT, false);
}

// ===================== Layout =====================
static void LayoutControls(HWND hwnd, int w, int h)
{
    int pad = 10;

    int headerH = 48;
    int footerH = 90;
    int rowH = 28;
    int themeW = 56;

    int top = pad + (headerH - rowH) / 2;

    // Theme right
    if (hThemeButton) MoveWindow(hThemeButton, w - pad - themeW, top, themeW, rowH, TRUE);

    // Status beside theme
    int statusW = 220;
    int statusLeft = w - pad - themeW - pad - statusW;

    if (hStatusLabel) MoveWindow(hStatusLabel, statusLeft, top + 6, 56, rowH, TRUE);
    if (hStatusValue) MoveWindow(hStatusValue, statusLeft + 58, top + 6, statusW - 58, rowH, TRUE);

    // Body (chat)
    int bodyTop = pad + headerH + pad;
    int bodyBottom = h - pad - footerH - pad;
    int bodyH = bodyBottom - bodyTop;

    if (hChatLog)
        MoveWindow(hChatLog, pad, bodyTop, w - pad * 2, (bodyH > 50 ? bodyH : 50), TRUE);

    // Footer (input + buttons)
    int inputTop = h - pad - footerH;
    int inputH = footerH;

    int sendW = 100;
    int discW = 120;
    int gap = 16;

    if (hInput)
        MoveWindow(hInput, pad, inputTop, w - pad * 2 - sendW - discW - gap * 2, inputH, TRUE);

    int sendLeft = pad + (w - pad * 2 - sendW - discW - gap * 2) + gap;

    if (hSendButton) MoveWindow(hSendButton, sendLeft, inputTop, sendW, inputH, TRUE);
    if (hDisconnectButton) MoveWindow(hDisconnectButton, sendLeft + sendW + gap, inputTop, discW, inputH, TRUE);

    if (hInputPlaceholder)
    {
        MoveWindow(
            hInputPlaceholder,
            pad + 12,
            inputTop + 12,
            (w - pad * 2 - sendW - discW - gap * 2) - 24,
            inputH - 24,
            TRUE
        );
        SetWindowPos(hInputPlaceholder, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    UpdateInputPlaceholderVisibility();
}

static void InvalidateAllButtons()
{
    if (hSendButton) InvalidateRect(hSendButton, nullptr, TRUE);
    if (hDisconnectButton) InvalidateRect(hDisconnectButton, nullptr, TRUE);
    if (hThemeButton) InvalidateRect(hThemeButton, nullptr, TRUE);
}

// ===================== Shared Memory Logic (same idea) =====================
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

void DoDisconnect()
{
    if (!g_connected) return;

    // send "left"
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

    AppendRichLine(LineKind::System, L"", L"[*] You disconnected.");
    UpdateStatusUI(L"Disconnected");

    Cleanup();

    g_hasUsername = false;
    g_username.clear();
    g_usernameW.clear();

    if (g_hwndMain)
        SetWindowTextW(g_hwndMain, L"Shared Memory Chat (GUI)");

    InvalidateAllButtons();
}

// Thread استقبال (نفس اللوجيك + ring fix)
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

        // لو اتخطينا الـ ring
        if (total - lastSeen > MAX_MESSAGES)
            lastSeen = total - MAX_MESSAGES;

        for (int i = lastSeen; i < total; ++i)
        {
            int idx = i % MAX_MESSAGES; // ✅ fix ring
            newMessages.push_back(g_shared->messages[idx]);
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

            // System detection
            if (wsText.rfind(L"[*]", 0) == 0 || wsText.rfind(L"[", 0) == 0)
            {
                wchar_t* copy = new wchar_t[wsText.size() + 1];
                wcscpy_s(copy, wsText.size() + 1, wsText.c_str());
                PostMessageW(hwnd, WM_NEW_MESSAGE, 1 /*system*/, (LPARAM)copy);
            }
            else
            {
                // format with who + msg
                wstring packed = wsSender + L"\n" + wsText;
                wchar_t* copy = new wchar_t[packed.size() + 1];
                wcscpy_s(copy, packed.size() + 1, packed.c_str());
                PostMessageW(hwnd, WM_NEW_MESSAGE, 0 /*normal*/, (LPARAM)copy);
            }
        }

        Sleep(100);
    }
}

// Enter to Send (Shift+Enter new line)
LRESULT CALLBACK InputProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift)
        {
            HWND parent = GetParent(hWnd);
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(3, BN_CLICKED), (LPARAM)hSendButton);
            return 0;
        }
    }

    if (msg == WM_CHAR && wParam == VK_RETURN)
    {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift) return 0;
    }

    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_CHAR || msg == WM_KEYUP || msg == WM_PASTE || msg == WM_CUT)
        UpdateInputPlaceholderVisibility();

    return CallWindowProcW(g_oldInputProc, hWnd, msg, wParam, lParam);
}

// ===================== Window Proc =====================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static thread receiverThread;

    switch (msg)
    {
    case WM_CREATE:
    {
        g_hwndMain = hwnd;

        LoadLibraryW(L"Msftedit.dll");

        g_brBg = CreateSolidBrush(CLR_BG);
        g_brEditBg = CreateSolidBrush(CLR_EDIT_BG);

        g_font = CreateFontW(
            18, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        g_emojiFont = CreateFontW(
            20, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Emoji"
        );

        g_placeholderFont = CreateFontW(
            20, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        auto ApplyFont = [&](HWND h) {
            if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
            };

        // Theme button (ID=13)
        hThemeButton = CreateWindowW(L"BUTTON", L"🌙",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 56, 28, hwnd, (HMENU)13, nullptr, nullptr);

        g_btnTheme.hwnd = hThemeButton;
        RegisterFancyButton(hThemeButton, BtnKind::Secondary);
        SubclassButton(hThemeButton);

        // Status
        hStatusLabel = CreateWindowW(L"STATIC", L"Status:", WS_CHILD | WS_VISIBLE, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        hStatusValue = CreateWindowW(L"STATIC", L"Disconnected", WS_CHILD | WS_VISIBLE, 0, 0, 150, 20, hwnd, nullptr, nullptr, nullptr);

        // Chat log (RichEdit)
        hChatLog = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"RICHEDIT50W",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 40, 560, 210,
            hwnd, (HMENU)1, nullptr, nullptr
        );

        // Input (multi-line)
        hInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | WS_VSCROLL,
            10, 260, 370, 55, hwnd, (HMENU)2, nullptr, nullptr);

        hInputPlaceholder = CreateWindowExW(
            WS_EX_TRANSPARENT,
            L"STATIC",
            INPUT_PLACEHOLDER,
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            0, 0, 0, 0,
            hwnd,
            (HMENU)2003,
            nullptr,
            nullptr
        );

        g_oldInputProc = (WNDPROC)SetWindowLongPtrW(hInput, GWLP_WNDPROC, (LONG_PTR)InputProc);

        // Buttons
        hSendButton = CreateWindowW(L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            390, 260, 80, 25, hwnd, (HMENU)3, nullptr, nullptr);

        g_btnSend.hwnd = hSendButton;
        RegisterFancyButton(hSendButton, BtnKind::Secondary);
        SubclassButton(hSendButton);

        hDisconnectButton = CreateWindowW(L"BUTTON", L"Disconnect",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            480, 260, 90, 25, hwnd, (HMENU)4, nullptr, nullptr);

        g_btnDisconnect.hwnd = hDisconnectButton;
        RegisterFancyButton(hDisconnectButton, BtnKind::Danger);
        SubclassButton(hDisconnectButton);

        // Fonts
        ApplyFont(hChatLog);
        ApplyFont(hInput);
        ApplyFont(hSendButton);
        ApplyFont(hDisconnectButton);
        ApplyFont(hStatusLabel);
        ApplyFont(hStatusValue);

        if (hThemeButton && g_emojiFont) SendMessageW(hThemeButton, WM_SETFONT, (WPARAM)g_emojiFont, TRUE);
        if (hInputPlaceholder) SendMessageW(hInputPlaceholder, WM_SETFONT, (WPARAM)g_placeholderFont, TRUE);

        // Tooltips
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);

        g_tooltip = CreateWindowExW(0, TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, nullptr, nullptr);

        AddToolTip(g_tooltip, hSendButton, L"Send message");
        AddToolTip(g_tooltip, hDisconnectButton, L"Disconnect");
        AddToolTip(g_tooltip, hThemeButton, L"Toggle Dark/Light Mode");

        // Layout
        RECT rc{};
        GetClientRect(hwnd, &rc);
        LayoutControls(hwnd, rc.right - rc.left, rc.bottom - rc.top);
        UpdateInputPlaceholderVisibility();

        ApplyTheme(false);

        // ================= Shared Memory init (same logic) =================
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
            AppendRichLine(LineKind::System, L"", L"[Failed to create/open shared memory]");
            UpdateStatusUI(L"Error");
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
            AppendRichLine(LineKind::System, L"", L"[Failed to map shared memory]");
            UpdateStatusUI(L"Error");
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
            1,  // initial
            1,  // max
            SEM_MUTEX_NAME
        );

        if (!g_hMutex)
        {
            AppendRichLine(LineKind::System, L"", L"[Failed to create/open mutex semaphore]");
            UpdateStatusUI(L"Error");
            EnableWindow(hSendButton, FALSE);
            EnableWindow(hDisconnectButton, FALSE);
            Cleanup();
            break;
        }

        g_connected = true;
        g_running = true;

        UpdateStatusUI(L"Connected");
        AppendRichLine(LineKind::System, L"", L"[Connected to Shared Memory Chat]");
        AppendRichLine(LineKind::System, L"", L"----------------------------------------");
        AppendRichLine(LineKind::System, L"", L"[Enter your username then press Send]");

        EnableWindow(hDisconnectButton, TRUE);
        EnableWindow(hSendButton, FALSE); // هيتفعل لما يكتب
        InvalidateAllButtons();

        receiverThread = thread(ReceiverThread, hwnd);
        receiverThread.detach();

        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlType == ODT_BUTTON)
        {
            BtnState* st = GetBtnState(dis->hwndItem);
            if (st)
            {
                DrawFancyButton(dis, st);
                return TRUE;
            }
        }
        break;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        LayoutControls(hwnd, w, h);
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HWND hCtl = (HWND)lParam;

        if (hCtl == hSendButton || hCtl == hDisconnectButton || hCtl == hThemeButton)
            return (INT_PTR)GetStockObject(NULL_BRUSH);

        HDC hdc = (HDC)wParam;

        if (hCtl == hInputPlaceholder)
            SetTextColor(hdc, CLR_HINT);
        else
            SetTextColor(hdc, CLR_TEXT);

        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)g_brBg;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_TEXT);
        SetBkColor(hdc, CLR_EDIT_BG);
        return (INT_PTR)g_brEditBg;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_brBg);
        return 1;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD notif = HIWORD(wParam);
        HWND from = (HWND)lParam;

        if (notif == STN_CLICKED)
        {
            if ((HWND)lParam == hInputPlaceholder) { SetFocus(hInput); UpdateInputPlaceholderVisibility(); break; }
        }

        if (id == 13) // Theme
        {
            ApplyTheme(!g_darkMode);
            InvalidateAllButtons();
            break;
        }

        if (from == hInput && (notif == EN_CHANGE || notif == EN_SETFOCUS || notif == EN_KILLFOCUS))
        {
            int len = GetWindowTextLengthW(hInput);
            bool hasTextNow = (len > 0);

            UpdateInputPlaceholderVisibility();

            EnableWindow(hSendButton, g_connected && hasTextNow);
            InvalidateRect(hSendButton, nullptr, TRUE);
        }

        if (id == 3 && g_connected) // Send
        {
            wchar_t buffer[512];
            GetWindowTextW(hInput, buffer, 512);
            if (wcslen(buffer) == 0) break;

            wstring wmsg = MakeSingleLine(buffer);
            TrimCRLF(wmsg);
            if (wmsg.empty()) break;

            string msgNarrow(wmsg.begin(), wmsg.end());

            // أول Send = Username (نفس منطقك)
            if (!g_hasUsername)
            {
                g_username = msgNarrow;
                g_usernameW = wmsg;
                if (g_username.empty())
                {
                    g_username = "User";
                    g_usernameW = L"User";
                }

                string joined = g_username + " joined the chat.";

                WaitForSingleObject(g_hMutex, INFINITE);
                int idx = g_shared->messageCount % MAX_MESSAGES;
                strncpy_s(g_shared->messages[idx].sender, g_username.c_str(), _TRUNCATE);
                strncpy_s(g_shared->messages[idx].text, joined.c_str(), _TRUNCATE);
                g_shared->messageCount++;
                ReleaseSemaphore(g_hMutex, 1, nullptr);

                g_hasUsername = true;

                AppendRichLine(LineKind::System, L"", L"----------------------------------------");
                AppendRichLine(LineKind::System, L"", wstring(L"[*] You joined as: ") + g_usernameW);

                wstring title = L"Shared Memory Chat (GUI) - " + g_usernameW;
                SetWindowTextW(hwnd, title.c_str());

                SetWindowTextW(hInput, L"");
            }
            else
            {
                // كتابة الرسالة في shared memory (نفس منطقك)
                WaitForSingleObject(g_hMutex, INFINITE);
                int idx = g_shared->messageCount % MAX_MESSAGES;
                strncpy_s(g_shared->messages[idx].sender, g_username.c_str(), _TRUNCATE);
                strncpy_s(g_shared->messages[idx].text, msgNarrow.c_str(), _TRUNCATE);
                g_shared->messageCount++;
                ReleaseSemaphore(g_hMutex, 1, nullptr);

                // عرض الرسالة محلياً مرة واحدة فقط (نفس منطقك)
                AppendRichLine(LineKind::You, wstring(L"You (") + g_usernameW + L")", wmsg);

                SetWindowTextW(hInput, L"");
            }

            UpdateInputPlaceholderVisibility();
            EnableWindow(hSendButton, FALSE);
            InvalidateRect(hSendButton, nullptr, TRUE);
            SetFocus(hInput);
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
        wstring payload = text ? text : L"";
        delete[] text;

        if ((int)wParam == 1)
        {
            // system msg
            AppendRichLine(LineKind::System, L"", payload);
        }
        else
        {
            // payload: "sender\ntext"
            size_t pos = payload.find(L'\n');
            if (pos == wstring::npos)
            {
                AppendRichLine(LineKind::Other, L"User", payload);
            }
            else
            {
                wstring who = payload.substr(0, pos);
                wstring msgText = payload.substr(pos + 1);
                AppendRichLine(LineKind::Other, who, msgText);
            }
        }
        break;
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

        if (g_tooltip) { DestroyWindow(g_tooltip); g_tooltip = nullptr; }

        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        if (g_emojiFont) { DeleteObject(g_emojiFont); g_emojiFont = nullptr; }
        if (g_placeholderFont) { DeleteObject(g_placeholderFont); g_placeholderFont = nullptr; }

        if (g_brBg) { DeleteObject(g_brBg); g_brBg = nullptr; }
        if (g_brEditBg) { DeleteObject(g_brEditBg); g_brEditBg = nullptr; }

        PostQuitMessage(0);
        break;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ===================== WinMain =====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    InitCommonControls();

    const wchar_t CLASS_NAME[] = L"SharedMemoryChatGUIClass";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Shared Memory Chat (GUI)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        780, 560,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    // Fade-in (زي كودك)
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    g_alpha = 0;
    SetLayeredWindowAttributes(hwnd, 0, g_alpha, LWA_ALPHA);
    SetTimer(hwnd, ID_TIMER_FADE, 20, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
