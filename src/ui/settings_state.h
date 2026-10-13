#pragma once
#include "settings.h"
#include "gpu_import.h"
namespace v0 {
// Stable Win32 IDs shared by command dispatch and layout; keep numbers out of UI code.
enum ControlId {
    ScreenSelector = 10,
    ChooseVideoButton,
    RemoveVideoButton,
    PauseButton,
    RefreshScreensButton,
    QuitButton,
    CancelImportButton,
    ScalingSelector,
    VideoPathField,
    StatusLabel,
    MemoryLabel,
    ImportProgress,
    ImportProfileSelector,
    ImportProfileLabel,
};
struct Card {
    std::wstring device;
    HWND window = nullptr;
    RECT bounds{};
};
constexpr COLORREF Background = RGB(245, 247, 251), Ink = RGB(29, 39, 55), Accent = RGB(45, 104, 220);

extern HWND panel, screens, path, status, memory, progressBar;
extern HFONT font, titleFont;
extern HBRUSH backgroundBrush;
extern UINT dpi;
extern std::vector<Screen> displays;
extern std::vector<Card> cards;
extern std::wstring message;
State* Target(const std::wstring& device);
void Stop(State& target);
void Assign(const std::wstring& device, const std::wstring& video, const std::wstring& source);
void Error(Failure failure);
void ReleasePreviews();
void BindPreviews();
void BuildCards();
void PaintPanel(HWND window);
void DrawButton(const DRAWITEMSTRUCT& item);
LRESULT CALLBACK ProgressProc(HWND, UINT, WPARAM, LPARAM);
} // namespace v0
