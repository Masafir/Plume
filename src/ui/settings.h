#pragma once
#include "../app_state.h"
void OpenSettings();
void RefreshSettings(bool rebuild = false);
void TickSettings();
void ChooseV0Screen(const Screen&);
void HandleV0Error(State&);
void CleanupV0();
HWND SettingsWindow();
void PrepareScreen(const std::wstring& source, const std::wstring& device);
void ShowSettingsError(Failure failure);
