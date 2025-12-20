#define WIN32_LEAN_AND_MEAN
#define BTN_ANIM_TIMER  101

#ifndef EM_HIDESELECTION
#define EM_HIDESELECTION (WM_USER + 63)
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <Richedit.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <cwchar>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER (ECM_FIRST + 1)
#endif

using namespace std;

// -------------------- GUI Controls --------------------
HWND hChatLog = nullptr;         // RichEdit
HWND hInput = nullptr;
HWND hSendButton = nullptr;
HWND hDisconnectButton = nullptr;

HWND hIpEdit = nullptr;
HWND hPortEdit = nullptr;
HWND hConnectButton = nullptr;

HWND hThemeButton = nullptr;     // 🌙 / ☀

HWND hIpPlaceholder = nullptr;
HWND hPortPlaceholder = nullptr;
HWND hInputPlaceholder = nullptr;

static HFONT g_placeholderFont = nullptr;
static HWND hIpLabel = nullptr;
static HWND hPortLabel = nullptr;

// Header / Status
static HWND hStatusLabel = nullptr;
static HWND hStatusValue = nullptr;

static HFONT g_font = nullptr;
static HFONT g_emojiFont = nullptr;

static HWND   g_tooltip = nullptr;
static WNDPROC g_oldInputProc = nullptr;
static HWND g_hwndMain = nullptr;

// -------------------- Theme --------------------
static bool g_darkMode = false;

// Light (default)
static COLORREF CLR_BG = RGB(255, 255, 255);
static COLORREF CLR_EDIT_BG = RGB(255, 255, 255);
static COLORREF CLR_TEXT = RGB(30, 30, 30);
static COLORREF CLR_HINT = RGB(140, 140, 140);

static COLORREF CLR_SYS = RGB(100, 116, 139);
static COLORREF CLR_SERVER = RGB(220, 38, 38);
static COLORREF CLR_YOU = RGB(22, 163, 74);

// Dark palette
static COLORREF CLR_BG_DARK = RGB(18, 18, 18);
static COLORREF CLR_EDIT_BG_DARK = RGB(28, 28, 28);
static COLORREF CLR_TEXT_DARK = RGB(235, 235, 235);
static COLORREF CLR_HINT_DARK = RGB(160, 160, 160);

static COLORREF CLR_SYS_DARK = RGB(148, 163, 184);
static COLORREF CLR_SERVER_DARK = RGB(248, 113, 113);
static COLORREF CLR_YOU_DARK = RGB(74, 222, 128);

static const wchar_t* INPUT_PLACEHOLDER = L"Type a message...";
static const wchar_t* IP_PLACEHOLDER = L"127.0.0.1";
static const wchar_t* PORT_PLACEHOLDER = L"54000";

static HBRUSH g_brBg = nullptr;
static HBRUSH g_brEditBg = nullptr;

// -------------------- Network --------------------
SOCKET sock = INVALID_SOCKET;
bool connected = false;

// Username state
bool hasUsername = false;
wstring g_username;

// Custom Windows Message for new incoming text
#define WM_NEW_MESSAGE (WM_APP + 1)

// -------- Fancy Buttons --------
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

static BtnState g_btnConnect{};
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

// ✅ FIX: GetBtnState يعتمد فقط على HWND المتسجل مسبقًا
static BtnState* GetBtnState(HWND h)
{
    if (g_btnConnect.hwnd == h) return &g_btnConnect;
    if (g_btnSend.hwnd == h) return &g_btnSend;
    if (g_btnDisconnect.hwnd == h) return &g_btnDisconnect;
    if (g_btnTheme.hwnd == h) return &g_btnTheme;
    return nullptr;
}

// ✅ FIX: RegisterFancyButton مفيهوش أي توزيع عشوائي
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

    // منع الضغط على Connect وهو Connected (من غير ما يبقى Disabled)
    if (st && hWnd == hConnectButton && connected)
    {
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)
            return 0;
    }

    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
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
    }

    case WM_MOUSELEAVE:
    {
        if (st)
        {
            st->hovered = false;
            st->pressed = false;

            st->animTarget = 0;
            SetTimer(hWnd, BTN_ANIM_TIMER, 15, nullptr);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    }

    case WM_TIMER:
    {
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
    }

    case WM_LBUTTONDOWN:
    {
        if (st)
        {
            st->pressed = true;
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    }

    case WM_LBUTTONUP:
    {
        if (st)
        {
            st->pressed = false;
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    }

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

// -------------------- Helpers --------------------
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
    {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            ch = L' ';
    }
    return s;
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

static bool IsValidPort(int port)
{
    return port >= 1 && port <= 65535;
}

static bool IsValidIPv4(const wchar_t* ip)
{
    IN_ADDR addr{};
    return InetPtonW(AF_INET, ip, &addr) == 1;
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

static void UpdateIpPortPlaceholders()
{
    if (!hIpEdit || !hPortEdit) return;

    if (hIpPlaceholder)
    {
        int len = GetWindowTextLengthW(hIpEdit);
        bool show = (len == 0) && (GetFocus() != hIpEdit);
        ShowWindow(hIpPlaceholder, show ? SW_SHOW : SW_HIDE);
    }

    if (hPortPlaceholder)
    {
        int len = GetWindowTextLengthW(hPortEdit);
        bool show = (len == 0) && (GetFocus() != hPortEdit);
        ShowWindow(hPortPlaceholder, show ? SW_SHOW : SW_HIDE);
    }
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
        CLR_SERVER = RGB(220, 38, 38);
        CLR_YOU = RGB(22, 163, 74);
    }
    else
    {
        CLR_BG = CLR_BG_DARK;
        CLR_EDIT_BG = CLR_EDIT_BG_DARK;
        CLR_TEXT = CLR_TEXT_DARK;
        CLR_HINT = CLR_HINT_DARK;

        CLR_SYS = CLR_SYS_DARK;
        CLR_SERVER = CLR_SERVER_DARK;
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
            hChatLog, hInput, hIpEdit, hPortEdit,
            hIpLabel, hPortLabel, hStatusLabel, hStatusValue,
            hIpPlaceholder, hPortPlaceholder, hInputPlaceholder,
            hConnectButton, hSendButton, hDisconnectButton, hThemeButton
        };

        for (HWND k : kids)
            if (k) InvalidateRect(k, nullptr, TRUE);

        UpdateInputPlaceholderVisibility();
        UpdateIpPortPlaceholders();

        // ✅ مهم: لو متصل، خلي Connect يفضل Success
        if (connected)
        {
            g_btnConnect.kind = BtnKind::Success;
            InvalidateRect(hConnectButton, nullptr, TRUE);
        }
    }
}

// -------------------- RichEdit append --------------------
static void RichAppendWithFormat(HWND hRe, const wstring& text, COLORREF color, bool bold, bool link = false)
{
    if (!hRe) return;

    int len = GetWindowTextLengthW(hRe);
    SendMessageW(hRe, EM_SETSEL, len, len);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_LINK;
    cf.crTextColor = color;
    cf.dwEffects = 0;
    if (bold) cf.dwEffects |= CFE_BOLD;
    if (link) cf.dwEffects |= CFE_LINK;

    SendMessageW(hRe, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hRe, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());

    int newLen = GetWindowTextLengthW(hRe);
    SendMessageW(hRe, EM_SETSEL, newLen, newLen);
    SendMessageW(hRe, EM_SCROLLCARET, 0, 0);
}

enum class LineKind { System, Server, You };

static void AppendRichLine(LineKind kind, const wstring& who, const wstring& msg)
{
    wstring ts = NowTimeStamp();
    wstring nl = L"\r\n";

    COLORREF nameClr = CLR_SYS;
    COLORREF msgClr = CLR_SYS;

    if (kind == LineKind::Server) { nameClr = CLR_SERVER; msgClr = CLR_TEXT; }
    if (kind == LineKind::You) { nameClr = CLR_YOU;    msgClr = CLR_TEXT; }
    if (kind == LineKind::System) { nameClr = CLR_SYS;    msgClr = CLR_SYS; }

    RichAppendWithFormat(hChatLog, ts, CLR_HINT, false);

    if (!who.empty())
    {
        RichAppendWithFormat(hChatLog, who, nameClr, true);
        RichAppendWithFormat(hChatLog, L": ", CLR_HINT, false);
    }

    RichAppendWithFormat(hChatLog, msg, msgClr, false);
    RichAppendWithFormat(hChatLog, nl, CLR_HINT, false);
}

// -------------------- Layout: Header / Body / Footer --------------------
static void LayoutControls(HWND hwnd, int w, int h)
{
    int pad = 10;

    int headerH = 48;
    int footerH = 90;

    int rowH = 28;
    int ipW = 170;
    int portW = 90;
    int btnW = 120;
    int themeW = 56;

    int top = pad + (headerH - rowH) / 2;

    if (hIpLabel) MoveWindow(hIpLabel, pad, top + 6, 26, rowH, TRUE);
    if (hPortLabel) MoveWindow(hPortLabel, pad + 210, top + 6, 40, rowH, TRUE);

    if (hIpEdit) MoveWindow(hIpEdit, pad + 30, top, ipW, rowH, TRUE);
    if (hPortEdit) MoveWindow(hPortEdit, pad + 260, top, portW, rowH, TRUE);

    if (hConnectButton) MoveWindow(hConnectButton, w - pad - btnW, top, btnW, rowH, TRUE);
    if (hThemeButton)   MoveWindow(hThemeButton, w - pad - btnW - pad - themeW, top, themeW, rowH, TRUE);

    int statusW = 190;
    int statusLeft = w - pad - btnW - pad - themeW - pad - statusW;

    if (hStatusLabel) MoveWindow(hStatusLabel, statusLeft, top + 6, 56, rowH, TRUE);
    if (hStatusValue) MoveWindow(hStatusValue, statusLeft + 58, top + 6, statusW - 58, rowH, TRUE);

    if (hIpPlaceholder)
        MoveWindow(hIpPlaceholder, pad + 38, top + 5, ipW - 16, rowH - 8, TRUE);

    if (hPortPlaceholder)
        MoveWindow(hPortPlaceholder, pad + 268, top + 5, portW - 16, rowH - 8, TRUE);

    int bodyTop = pad + headerH + pad;
    int bodyBottom = h - pad - footerH - pad;
    int bodyH = bodyBottom - bodyTop;

    if (hChatLog)
        MoveWindow(hChatLog, pad, bodyTop, w - pad * 2, (bodyH > 50 ? bodyH : 50), TRUE);

    int inputTop = h - pad - footerH;
    int inputH = footerH;

    int sendW = 100;
    int discW = 120;

    // ✅ مسافة أكبر بين input و زرار send/disconnect
    int gap = 16; // كان 10 ضمنيًا، زودناه

    if (hInput)
        MoveWindow(hInput, pad, inputTop, w - pad * 2 - sendW - discW - gap * 2, inputH, TRUE);

    int sendLeft = pad + (w - pad * 2 - sendW - discW - gap * 2) + gap;

    if (hSendButton)
        MoveWindow(hSendButton, sendLeft, inputTop, sendW, inputH, TRUE);

    if (hDisconnectButton)
        MoveWindow(hDisconnectButton, sendLeft + sendW + gap, inputTop, discW, inputH, TRUE);

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
    }

    UpdateInputPlaceholderVisibility();
    UpdateIpPortPlaceholders();

    if (hIpPlaceholder) SetWindowPos(hIpPlaceholder, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (hPortPlaceholder) SetWindowPos(hPortPlaceholder, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (hInputPlaceholder) SetWindowPos(hInputPlaceholder, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

static void InvalidateAllButtons()
{
    if (hConnectButton) InvalidateRect(hConnectButton, nullptr, TRUE);
    if (hSendButton) InvalidateRect(hSendButton, nullptr, TRUE);
    if (hDisconnectButton) InvalidateRect(hDisconnectButton, nullptr, TRUE);
    if (hThemeButton) InvalidateRect(hThemeButton, nullptr, TRUE);
}

void DoDisconnect()
{
    if (!connected) return;

    connected = false;

    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    WSACleanup();

    AppendRichLine(LineKind::System, L"", L"[*] Disconnected.");
    UpdateStatusUI(L"Disconnected");

    EnableWindow(hSendButton, FALSE);
    EnableWindow(hDisconnectButton, FALSE);

    EnableWindow(hIpEdit, TRUE);
    EnableWindow(hPortEdit, TRUE);

    g_btnConnect.hovered = g_btnConnect.pressed = false;
    g_btnConnect.anim = 0;
    g_btnConnect.animTarget = 0;
    g_btnConnect.kind = BtnKind::Primary;
    SetWindowTextW(hConnectButton, L"Connect");
    EnableWindow(hConnectButton, TRUE);

    g_btnSend.hovered = g_btnSend.pressed = false;
    g_btnDisconnect.hovered = g_btnDisconnect.pressed = false;

    UpdateIpPortPlaceholders();
    InvalidateAllButtons();

    hasUsername = false;
    g_username.clear();

    if (g_hwndMain)
        SetWindowTextW(g_hwndMain, L"Chat Client (GUI)");
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
            wchar_t* copy = new wchar_t[64];
            wcscpy_s(copy, 64, L"[*] Connection closed by server.");
            PostMessage(hwnd, WM_NEW_MESSAGE, 0, (LPARAM)copy);
            PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(4, BN_CLICKED), 0);
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
        if (!shift)
            return 0;
    }

    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_CHAR || msg == WM_KEYUP || msg == WM_PASTE || msg == WM_CUT)
    {
        UpdateInputPlaceholderVisibility();
    }

    return CallWindowProcW(g_oldInputProc, hWnd, msg, wParam, lParam);
}

// -------------------- Button drawing --------------------
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

        if (st->pressed)
            fill = Adjust(baseFill, -25);
        else if (st->hovered)
            fill = Adjust(baseFill, +hoverBoost);
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

// -------------------- WM_NOTIFY for RichEdit links --------------------
static void HandleRichLinkClick(HWND hRe, ENLINK* enl)
{
    if (!hRe || !enl) return;
    if (enl->msg != WM_LBUTTONUP) return;

    TEXTRANGEW tr{};
    tr.chrg = enl->chrg;

    int len = enl->chrg.cpMax - enl->chrg.cpMin;
    if (len <= 0) return;

    std::wstring url;
    url.resize(len);

    tr.lpstrText = &url[0];

    SendMessageW(hRe, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

    while (!url.empty() &&
        (url.back() == L'\0' || url.back() == L' ' ||
            url.back() == L'\r' || url.back() == L'\n'))
    {
        url.pop_back();
    }

    if (!url.empty())
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// -------------------- Window procedure --------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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

        hIpLabel = CreateWindowW(L"STATIC", L"IP:", WS_CHILD | WS_VISIBLE, 10, 10, 20, 20, hwnd, nullptr, nullptr, nullptr);
        hPortLabel = CreateWindowW(L"STATIC", L"Port:", WS_CHILD | WS_VISIBLE, 180, 10, 40, 20, hwnd, nullptr, nullptr, nullptr);

        hIpEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 35, 8, 130, 22, hwnd, (HMENU)10, nullptr, nullptr);

        hPortEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 220, 8, 70, 22, hwnd, (HMENU)11, nullptr, nullptr);

        hIpPlaceholder = CreateWindowExW(
            WS_EX_TRANSPARENT,
            L"STATIC",
            IP_PLACEHOLDER,
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            0, 0, 0, 0,
            hwnd,
            (HMENU)2001,
            nullptr,
            nullptr
        );

        hPortPlaceholder = CreateWindowExW(
            WS_EX_TRANSPARENT,
            L"STATIC",
            PORT_PLACEHOLDER,
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            0, 0, 0, 0,
            hwnd,
            (HMENU)2002,
            nullptr,
            nullptr
        );

        // Theme Toggle Button (ID = 13)
        hThemeButton = CreateWindowW(L"BUTTON", L"🌙",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 56, 28, hwnd, (HMENU)13, nullptr, nullptr);

        // ✅ FIX: ربط hwnd بالـ state قبل Register
        g_btnTheme.hwnd = hThemeButton;
        RegisterFancyButton(hThemeButton, BtnKind::Secondary);
        SubclassButton(hThemeButton);

        hConnectButton = CreateWindowW(L"BUTTON", L"Connect",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 300, 8, 80, 22, hwnd, (HMENU)12, nullptr, nullptr);

        // ✅ FIX: ربط hwnd بالـ state قبل Register
        g_btnConnect.hwnd = hConnectButton;
        RegisterFancyButton(hConnectButton, BtnKind::Primary);
        SubclassButton(hConnectButton);

        hStatusLabel = CreateWindowW(L"STATIC", L"Status:", WS_CHILD | WS_VISIBLE, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        hStatusValue = CreateWindowW(L"STATIC", L"Disconnected", WS_CHILD | WS_VISIBLE, 0, 0, 120, 20, hwnd, nullptr, nullptr, nullptr);

        hChatLog = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"RICHEDIT50W",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 40, 560, 210,
            hwnd, (HMENU)1, nullptr, nullptr
        );

        SendMessageW(hChatLog, EM_AUTOURLDETECT, TRUE, 0);
        SendMessageW(hChatLog, EM_SETEVENTMASK, 0, ENM_LINK);

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

        hSendButton = CreateWindowW(L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 390, 260, 80, 25, hwnd, (HMENU)3, nullptr, nullptr);

        // ✅ FIX: ربط hwnd بالـ state قبل Register
        g_btnSend.hwnd = hSendButton;
        RegisterFancyButton(hSendButton, BtnKind::Secondary);
        SubclassButton(hSendButton);

        hDisconnectButton = CreateWindowW(L"BUTTON", L"Disconnect",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 480, 260, 90, 25, hwnd, (HMENU)4, nullptr, nullptr);

        // ✅ FIX: ربط hwnd بالـ state قبل Register
        g_btnDisconnect.hwnd = hDisconnectButton;
        RegisterFancyButton(hDisconnectButton, BtnKind::Danger);
        SubclassButton(hDisconnectButton);

        EnableWindow(hSendButton, FALSE);
        EnableWindow(hDisconnectButton, FALSE);

        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);

        g_tooltip = CreateWindowExW(0, TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, nullptr, nullptr);

        AddToolTip(g_tooltip, hIpEdit, L"Enter server IP address (e.g., 127.0.0.1)");
        AddToolTip(g_tooltip, hPortEdit, L"Enter server Port (1 - 65535)");
        AddToolTip(g_tooltip, hConnectButton, L"Connect to server");
        AddToolTip(g_tooltip, hSendButton, L"Send message");
        AddToolTip(g_tooltip, hDisconnectButton, L"Disconnect from server");
        AddToolTip(g_tooltip, hThemeButton, L"Toggle Dark/Light Mode");

        ApplyFont(hIpLabel);
        ApplyFont(hPortLabel);
        ApplyFont(hIpEdit);
        ApplyFont(hPortEdit);
        ApplyFont(hConnectButton);
        ApplyFont(hChatLog);
        ApplyFont(hInput);
        ApplyFont(hSendButton);
        ApplyFont(hDisconnectButton);
        ApplyFont(hStatusLabel);
        ApplyFont(hStatusValue);

        if (hThemeButton && g_emojiFont) SendMessageW(hThemeButton, WM_SETFONT, (WPARAM)g_emojiFont, TRUE);

        if (hIpPlaceholder) SendMessageW(hIpPlaceholder, WM_SETFONT, (WPARAM)g_placeholderFont, TRUE);
        if (hPortPlaceholder) SendMessageW(hPortPlaceholder, WM_SETFONT, (WPARAM)g_placeholderFont, TRUE);
        if (hInputPlaceholder) SendMessageW(hInputPlaceholder, WM_SETFONT, (WPARAM)g_placeholderFont, TRUE);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        LayoutControls(hwnd, rc.right - rc.left, rc.bottom - rc.top);

        InvalidateAllButtons();
        UpdateInputPlaceholderVisibility();
        UpdateIpPortPlaceholders();

        ApplyTheme(false);
        AppendRichLine(LineKind::System, L"", L"[*] Ready.");
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

    case WM_NOTIFY:
    {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm && nm->hwndFrom == hChatLog && nm->code == EN_LINK)
        {
            ENLINK* enl = (ENLINK*)lParam;
            HandleRichLinkClick(hChatLog, enl);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HWND hCtl = (HWND)lParam;

        if (hCtl == hConnectButton || hCtl == hSendButton || hCtl == hDisconnectButton || hCtl == hThemeButton)
            return (INT_PTR)GetStockObject(NULL_BRUSH);

        HDC hdc = (HDC)wParam;

        if (hCtl == hInputPlaceholder || hCtl == hIpPlaceholder || hCtl == hPortPlaceholder)
            SetTextColor(hdc, CLR_HINT);
        else
            SetTextColor(hdc, CLR_TEXT);

        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)g_brBg;
    }

    case WM_CTLCOLOREDIT:
    {
        HWND hCtl = (HWND)lParam;

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
            if ((HWND)lParam == hIpPlaceholder) { SetFocus(hIpEdit); UpdateIpPortPlaceholders(); break; }
            if ((HWND)lParam == hPortPlaceholder) { SetFocus(hPortEdit); UpdateIpPortPlaceholders(); break; }
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

            EnableWindow(hSendButton, connected && hasTextNow);
            InvalidateRect(hSendButton, nullptr, TRUE);
        }

        if ((from == hIpEdit || from == hPortEdit) &&
            (notif == EN_CHANGE || notif == EN_SETFOCUS || notif == EN_KILLFOCUS))
        {
            UpdateIpPortPlaceholders();
        }

        // CONNECT
        if (id == 12)
        {
            if (connected) break;

            wchar_t ipBuf[64], portBuf[16];
            GetWindowTextW(hIpEdit, ipBuf, 64);
            GetWindowTextW(hPortEdit, portBuf, 16);

            if (wcslen(ipBuf) == 0)
            {
                wcscpy_s(ipBuf, IP_PLACEHOLDER);
                SetWindowTextW(hIpEdit, ipBuf);
            }
            if (wcslen(portBuf) == 0)
            {
                wcscpy_s(portBuf, PORT_PLACEHOLDER);
                SetWindowTextW(hPortEdit, portBuf);
            }

            int port = _wtoi(portBuf);
            UpdateIpPortPlaceholders();

            if (!IsValidIPv4(ipBuf))
            {
                MessageBoxW(hwnd, L"Invalid IP address. Example: 127.0.0.1", L"Input Error", MB_ICONERROR);
                SetFocus(hIpEdit);
                break;
            }

            if (!IsValidPort(port))
            {
                MessageBoxW(hwnd, L"Invalid Port. Must be between 1 and 65535.", L"Input Error", MB_ICONERROR);
                SetFocus(hPortEdit);
                break;
            }

            string ip = string(ipBuf, ipBuf + wcslen(ipBuf));

            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            {
                AppendRichLine(LineKind::System, L"", L"[WSAStartup failed]");
                break;
            }

            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == INVALID_SOCKET)
            {
                AppendRichLine(LineKind::System, L"", L"[Socket creation failed]");
                WSACleanup();
                break;
            }

            sockaddr_in server{};
            server.sin_family = AF_INET;
            server.sin_port = htons((u_short)port);
            inet_pton(AF_INET, ip.c_str(), &server.sin_addr);

            if (connect(sock, (sockaddr*)&server, sizeof(server)) == 0)
            {
                connected = true;

                g_btnConnect.kind = BtnKind::Success;
                g_btnConnect.hovered = g_btnConnect.pressed = false;
                g_btnConnect.anim = 0;
                g_btnConnect.animTarget = 0;
                SetWindowTextW(hConnectButton, L"Connected");
                EnableWindow(hConnectButton, TRUE);
                InvalidateRect(hConnectButton, nullptr, TRUE);

                EnableWindow(hIpEdit, FALSE);
                EnableWindow(hPortEdit, FALSE);
                UpdateIpPortPlaceholders();

                UpdateStatusUI(L"Connected");

                AppendRichLine(LineKind::System, L"", L"[Connected to server]");
                AppendRichLine(LineKind::System, L"", L"[Enter username then press Send]");

                EnableWindow(hSendButton, FALSE);
                EnableWindow(hDisconnectButton, TRUE);

                g_btnSend.hovered = g_btnSend.pressed = false;
                g_btnDisconnect.hovered = g_btnDisconnect.pressed = false;
                InvalidateAllButtons();

                thread(ReceiverThread, hwnd).detach();

                int len = GetWindowTextLengthW(hInput);
                EnableWindow(hSendButton, (len > 0));
            }
            else
            {
                AppendRichLine(LineKind::System, L"", L"[Failed to connect]");

                closesocket(sock);
                sock = INVALID_SOCKET;
                WSACleanup();

                InvalidateAllButtons();
            }
        }
        // SEND
        else if (id == 3 && connected)
        {
            wchar_t buffer[512];
            GetWindowTextW(hInput, buffer, 512);
            if (wcslen(buffer) == 0) break;

            wstring wmsg = MakeSingleLine(buffer);
            TrimCRLF(wmsg);
            if (wmsg.empty()) break;

            if (!hasUsername)
            {
                g_username = wmsg;
                hasUsername = true;

                int size_needed = WideCharToMultiByte(CP_UTF8, 0, g_username.c_str(), -1, nullptr, 0, nullptr, nullptr);
                string msgUtf8(size_needed - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, g_username.c_str(), -1, &msgUtf8[0], size_needed - 1, nullptr, nullptr);

                send(sock, msgUtf8.c_str(), (int)msgUtf8.size(), 0);

                AppendRichLine(LineKind::System, L"", wstring(L"[*] Joined as: ") + g_username);

                wstring title = L"Chat Client - " + g_username;
                SetWindowTextW(hwnd, title.c_str());
            }
            else
            {
                int needed = WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
                string msgUtf8(needed - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, &msgUtf8[0], needed - 1, nullptr, nullptr);

                send(sock, msgUtf8.c_str(), (int)msgUtf8.size(), 0);

                AppendRichLine(LineKind::You, wstring(L"You (") + g_username + L")", wmsg);
            }

            SetWindowTextW(hInput, L"");
            UpdateInputPlaceholderVisibility();

            EnableWindow(hSendButton, FALSE);
            InvalidateRect(hSendButton, nullptr, TRUE);

            SetFocus(hInput);
        }
        // DISCONNECT
        else if (id == 4)
        {
            DoDisconnect();
        }

        break;
    }

    case WM_NEW_MESSAGE:
    {
        wchar_t* text = (wchar_t*)lParam;
        wstring m = text ? text : L"";
        delete[] text;

        TrimCRLF(m);

        if (m.rfind(L"[*]", 0) == 0 || m.rfind(L"[", 0) == 0)
            AppendRichLine(LineKind::System, L"", m);
        else
            AppendRichLine(LineKind::Server, L"Server", m);

        break;
    }

    case WM_DESTROY:
    {
        DoDisconnect();

        if (g_tooltip) { DestroyWindow(g_tooltip); g_tooltip = nullptr; }

        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        if (g_emojiFont) { DeleteObject(g_emojiFont); g_emojiFont = nullptr; }

        if (g_brBg) { DeleteObject(g_brBg); g_brBg = nullptr; }
        if (g_brEditBg) { DeleteObject(g_brEditBg); g_brEditBg = nullptr; }
        if (g_placeholderFont) { DeleteObject(g_placeholderFont); g_placeholderFont = nullptr; }

        PostQuitMessage(0);
        break;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -------------------- WinMain --------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    InitCommonControls();

    const wchar_t CLASS_NAME[] = L"ChatClientGUIClass";

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Chat Client (GUI)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        780, 560,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
