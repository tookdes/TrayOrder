#pragma once

#include <windows.h>
#include <string>

namespace trayorder {

inline constexpr wchar_t kCommandMessageName[] = L"TrayOrder.Command.v1";
inline constexpr wchar_t kRegistryRoot[] = L"Software\\TrayOrder";
inline constexpr wchar_t kSettingsKey[] = L"Software\\TrayOrder\\Settings";
inline constexpr wchar_t kStateKey[] = L"Software\\TrayOrder\\State";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValueName[] = L"TrayOrder";
inline constexpr wchar_t kCanonicalValueName[] = L"CanonicalOrderV200";

inline constexpr UINT_PTR kTrayIconId = 1;
inline constexpr UINT kTrayCallbackMessage = WM_APP + 1;
inline constexpr UINT_PTR kTimerId = 1;

enum class Command : WPARAM {
    Attach = 1,
    ReloadSettings = 2,
    Shutdown = 3,
};

inline std::wstring ReadyEventName(DWORD explorerPid) {
    return L"Local\\TrayOrder.Ready." + std::to_wstring(explorerPid);
}

inline std::wstring FailedEventName(DWORD explorerPid) {
    return L"Local\\TrayOrder.Failed." + std::to_wstring(explorerPid);
}

inline std::wstring StoppedEventName(DWORD explorerPid) {
    return L"Local\\TrayOrder.Stopped." + std::to_wstring(explorerPid);
}

}  // namespace trayorder
