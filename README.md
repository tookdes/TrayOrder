# TrayOrder

TrayOrder is a small standalone Windows 11 utility that keeps notification-area
(system tray) icons in a stable user-defined order. It does **not** require
Windhawk.

The first release is intentionally narrow: preserve one canonical order, allow
explicit editing of that order, and restore returning icons without replacing
the Windows tray UI.

## How it works

TrayOrder has two components:

- `TrayOrder.exe` — a normal per-user tray application and settings controller.
- `TrayOrderHook.dll` — loaded into the Explorer taskbar thread with
  `SetWindowsHookEx(WH_GETMESSAGE)`.

The hook DLL uses Windows' own notification-area move path to restore icon
relations. It does not rewrite `UIOrderList` to force a live refresh and it does
not restart Explorer.

The ordering core is derived from Yusseter's MIT-licensed
[Tray Order Lock](https://github.com/Yusseter/tray-order-lock). The standalone
port replaces Windhawk's runtime services with:

- MinHook for in-process hooks.
- A small registry-backed settings/state store under `HKCU\\Software\\TrayOrder`.
- Microsoft public symbols for the local `taskbar.dll`.

On first use, the hook reads the RSDS record from the local `taskbar.dll`,
downloads the exact matching `taskbar.pdb` from Microsoft Symbol Server, and
caches it under `%LOCALAPPDATA%\\TrayOrder\\symbols`. No Microsoft symbol files
are bundled with releases.

## Usage

1. Put `TrayOrder.exe` and `TrayOrderHook.dll` in the same directory.
2. Arrange the tray roughly how you want it before the first launch.
3. Run `TrayOrder.exe`. The current order becomes the initial canonical order and
   the default mode is **Lock saved order**.
4. To change the order, right-click the TrayOrder icon and select
   **Allow reordering**. Drag icons normally in the Windows tray, then select
   **Lock saved order** again.

Tray menu options:

- **Lock saved order** — blocks manual tray reorder requests and restores known
  icons that return in the wrong canonical relation.
- **Allow reordering** — lets Windows perform manual moves; successful moves are
  learned into the canonical order.
- **New icons: Windows default** — adopt genuinely new icons where Windows places
  them.
- **New icons: place at end** — move new icons to the end before adopting them.
- **Use current order as baseline** — discard the saved canonical order and
  rebuild it from the current Windows tray order.
- **Start with Windows** — add/remove the per-user Run entry.
- **Open log folder** — opens `%LOCALAPPDATA%\\TrayOrder`.
- **Reload Explorer hook** — cleanly detach and reattach the hook.

Explorer restarts are detected automatically and the hook is reattached to the
new Explorer process.

## Scope

Current target: **Windows 11 x64**. The internal notification-area interfaces are
not a stable public API. TrayOrder resolves symbols for the exact local build and
fails closed if required symbols cannot be resolved uniquely.

The current core was originally validated by its upstream project on Windows 11
25H2 build 26200.8973. Other current Windows 11 builds are expected to work when
the same internal symbols remain available, but need real-machine validation.

This first version does not attempt to support Windows 10, ARM64, multiple
independent ordering profiles, or a full drag-and-drop configuration window.

## Build

Visual Studio 2022 / MSVC and CMake are supported:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

GitHub Actions also produces `TrayOrder-win64.zip` on every push and pull
request.

## Safety model

TrayOrder runs without elevation. The controller and Explorer run in the same
user session. The DLL is injected only into the Explorer taskbar thread. If the
controller exits normally, the private taskbar hooks are disabled before the
Windows hook is removed. The DLL also holds a temporary self-reference and
watches the controller process so an unexpected controller exit does not leave
patched code pointing at an unloaded DLL.

## Credits

See [NOTICE.md](NOTICE.md). In particular, the notification-area ordering core is
based on Tray Order Lock 0.2.0 by Yusseter.
