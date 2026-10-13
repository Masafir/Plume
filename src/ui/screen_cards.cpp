#include "settings_state.h"

namespace v0 {
std::vector<Card> cards;
HBRUSH backgroundBrush = nullptr;
HFONT titleFont = nullptr;
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == PBM_GETPOS)
        return GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == PBM_SETPOS) {
        auto previous = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        auto value = std::clamp<INT_PTR>(static_cast<INT_PTR>(w), 0, 100);
        if (previous != value) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, value);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return previous;
    }
    if (msg == PBM_SETRANGE32)
        return 0;
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        HBRUSH track = CreateSolidBrush(RGB(217, 225, 238)), fill = CreateSolidBrush(Accent);
        FillRect(dc, &r, track);
        r.right =
            r.left + MulDiv(r.right - r.left, static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)), 100);
        FillRect(dc, &r, fill);
        DeleteObject(track);
        DeleteObject(fill);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void ReleasePreviews() {
    auto clear = [](State& t) {
        std::lock_guard lock(t.previewMutex);
        t.preview = {};
        t.previewFrames = 0;
    };
    clear(*application);
    for (auto& child : application->children)
        clear(*child);
}
void BindPreviews() {
    for (const auto& card : cards)
        if (auto t = Target(card.device)) {
            std::lock_guard lock(t->previewMutex);
            if (t->preview.window != card.window) {
                t->preview = {};
                t->preview.window = card.window;
                t->previewFrames = 0;
            }
        }
}
LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_LBUTTONUP) {
        for (size_t i = 0; i < cards.size(); ++i)
            if (cards[i].window == hwnd) {
                SendMessageW(screens, CB_SETCURSEL, i, 0);
                RefreshSettings();
            }
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        State* t = nullptr;
        for (const auto& card : cards)
            if (card.window == hwnd)
                t = Target(card.device);
        if (!t || !t->previewFrames) {
            HBRUSH brush = CreateSolidBrush(RGB(22, 29, 42));
            FillRect(dc, &r, brush);
            DeleteObject(brush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(181, 191, 210));
            SelectObject(dc, font);
            const wchar_t* text = t && !t->options.video.empty() ? L"En attente d’une image…"
                                                                 : L"Choisir une vidéo pour cet écran";
            DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void BuildCards() {
    ReleasePreviews();
    for (auto& card : cards)
        DestroyWindow(card.window);
    cards.clear();
    WNDCLASSW wc{};
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PlumeV0.Preview";
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    RegisterClassW(&wc);
    // Three cards per row at most; the monitor combobox remains keyboard accessible.
    int columns = std::max(1, std::min(3, static_cast<int>(displays.size())));
    int cardWidth = columns == 1 ? 648 : columns == 2 ? 316 : 264;
    int clientWidth = 48 + columns * cardWidth + (columns - 1) * 16;
    int rows = (static_cast<int>(displays.size()) + columns - 1) / columns;
    int footer = 134 + rows * 212;
    RECT client{0, 0, MulDiv(clientWidth, dpi, 96), MulDiv(footer + 266, dpi, 96)};
    AdjustWindowRectExForDpi(&client, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0,
                             dpi);
    SetWindowPos(panel, nullptr, 0, 0, client.right - client.left, client.bottom - client.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    auto move = [&](int id, int x, int y, int width, int height) {
        SetWindowPos(GetDlgItem(panel, id), nullptr, MulDiv(x, dpi, 96), MulDiv(y, dpi, 96),
                     MulDiv(width, dpi, 96), MulDiv(height, dpi, 96), SWP_NOZORDER);
    };
    move(ScreenSelector, 24, 88, clientWidth - 48, 180);
    move(VideoPathField, 24, footer + 12, clientWidth - 48, 28);
    move(ImportProgress, 24, footer + 41, clientWidth - 48, 9);
    move(ChooseVideoButton, 24, footer + 54, 190, 36);
    move(RemoveVideoButton, 226, footer + 54, 190, 36);
    move(CancelImportButton, 428, footer + 54, clientWidth - 452, 36);
    move(PauseButton, 24, footer + 104, 102, 34);
    move(RefreshScreensButton, 138, footer + 104, 166, 34);
    move(ScalingSelector, 316, footer + 104, 190, 140);
    move(QuitButton, clientWidth - 132, footer + 104, 108, 34);
    move(ImportProfileLabel, 24, footer + 157, 166, 24);
    move(ImportProfileSelector, 196, footer + 151, clientWidth - 220, 140);
    move(StatusLabel, 24, footer + 198, clientWidth - 48, 36);
    move(MemoryLabel, 24, footer + 238, clientWidth - 48, 24);
    for (size_t i = 0; i < displays.size(); ++i) {
        int x = 24 + (static_cast<int>(i) % columns) * (cardWidth + 16),
            y = 134 + (static_cast<int>(i) / columns) * 212;
        RECT bounds{MulDiv(x, dpi, 96), MulDiv(y, dpi, 96), MulDiv(x + cardWidth, dpi, 96),
                    MulDiv(y + 196, dpi, 96)};
        HWND child =
            CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            MulDiv(x + 8, dpi, 96), MulDiv(y + 38, dpi, 96), MulDiv(cardWidth - 16, dpi, 96),
                            MulDiv(148, dpi, 96), panel, nullptr, wc.hInstance, nullptr);
        cards.push_back({displays[i].device, child, bounds});
    }
    BindPreviews();
    InvalidateRect(panel, nullptr, TRUE);
}
void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, backgroundBrush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Ink);
    SelectObject(dc, titleFont);
    RECT heading{MulDiv(24, dpi, 96), MulDiv(18, dpi, 96), client.right, MulDiv(52, dpi, 96)};
    DrawTextW(dc, L"Vos écrans, vos vidéos", -1, &heading, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, font);
    SetTextColor(dc, RGB(91, 105, 124));
    RECT subtitle{MulDiv(24, dpi, 96), MulDiv(57, dpi, 96), client.right, MulDiv(81, dpi, 96)};
    DrawTextW(dc, L"Sélectionnez un écran et choisissez son fond animé.", -1, &subtitle,
              DT_LEFT | DT_SINGLELINE);
    int selected = static_cast<int>(SendMessageW(screens, CB_GETCURSEL, 0, 0));
    for (size_t i = 0; i < cards.size(); ++i) {
        auto r = cards[i].bounds;
        HPEN pen = CreatePen(PS_SOLID, i == static_cast<size_t>(selected) ? 2 : 1,
                             i == static_cast<size_t>(selected) ? Accent : RGB(214, 222, 233));
        auto oldPen = SelectObject(dc, pen);
        auto oldBrush = SelectObject(dc, GetStockObject(WHITE_BRUSH));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, MulDiv(14, dpi, 96), MulDiv(14, dpi, 96));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        RECT label = r;
        label.left += MulDiv(12, dpi, 96);
        label.top += MulDiv(9, dpi, 96);
        label.bottom = label.top + MulDiv(22, dpi, 96);
        SetTextColor(dc, Ink);
        auto t = Target(cards[i].device);
        std::wstring text =
            L"Écran " + std::to_wstring(i + 1) + (displays[i].primary ? L" · principal" : L"");
        if (importing && importing->device == cards[i].device)
            text += L" · préparation…";
        else if (t && !t->options.video.empty())
            text += t->pause ? L" · en pause" : L" · lecture";
        else
            text += L" · sans vidéo";
        DrawTextW(dc, text.c_str(), -1, &label, DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    EndPaint(hwnd, &ps);
}
void DrawButton(const DRAWITEMSTRUCT& item) {
    bool primary = item.CtlID == ChooseVideoButton, disabled = (item.itemState & ODS_DISABLED) != 0,
         pressed = (item.itemState & ODS_SELECTED) != 0;
    COLORREF fill = disabled  ? RGB(231, 235, 242)
                    : primary ? (pressed ? RGB(30, 83, 185) : Accent)
                              : (pressed ? RGB(229, 234, 243) : RGB(255, 255, 255));
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : RGB(207, 216, 230));
    auto oldBrush = SelectObject(item.hDC, brush);
    auto oldPen = SelectObject(item.hDC, pen);
    auto r = item.rcItem;
    RoundRect(item.hDC, r.left, r.top, r.right, r.bottom, MulDiv(10, dpi, 96), MulDiv(10, dpi, 96));
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    wchar_t text[100]{};
    GetWindowTextW(item.hwndItem, text, 100);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? RGB(151, 161, 179) : primary ? RGB(255, 255, 255) : Ink);
    SelectObject(item.hDC, font);
    DrawTextW(item.hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) {
        InflateRect(&r, -4, -4);
        DrawFocusRect(item.hDC, &r);
    }
}

} // namespace v0
