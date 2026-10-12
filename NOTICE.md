# Third-party notices

The notification-area identity, canonical ordering, and restoration logic in
`src/tray_order_core.cpp` is derived from **Tray Order Lock 0.2.0** by Yusseter:
https://github.com/Yusseter/tray-order-lock

Tray Order Lock is distributed under the MIT License. Its original copyright
notice is retained in this repository's `LICENSE` and in the derived source.

TrayOrder also uses **MinHook** by Tsuda Kageyu and contributors at build time:
https://github.com/TsudaKageyu/minhook

MinHook is distributed under the 2-clause BSD license. See the upstream project
for its full license text.

No Microsoft symbol files are redistributed. TrayOrder downloads the matching
public PDB for the local `taskbar.dll` from Microsoft Symbol Server on first use
and caches it under the current user's local app-data directory.
