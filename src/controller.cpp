#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>

#include "interop.hpp"

namespace {
constexpr wchar_t kClassName[] = L"TrayOrder.Controller";
constexpr GUID kTrayGuid = {0xd742715e,0x05fc,0x4dbf,{0xa8,0x83,0x9d,0x37,0x41,0x13,0xb3,0x6e}};
enum : UINT { M_LOCK=100, M_EDIT, M_DEFAULT=110, M_END, M_BASELINE=120, M_STARTUP, M_LOG, M_RELOAD, M_EXIT };

HWND g_window{};
HHOOK g_hook{};
HMODULE g_dll{};
DWORD g_pid{}, g_tid{};
HANDLE g_ready{}, g_failed{}, g_stopped{};
UINT g_command{};
HANDLE g_singleton{};

std::wstring ExePath() {
    wchar_t p[32768]{};
    DWORD n=GetModuleFileNameW(nullptr,p,_countof(p));
    return n && n<_countof(p) ? std::wstring(p,n) : L"";
}
std::filesystem::path ExeDir(){ return std::filesystem::path(ExePath()).parent_path(); }
std::filesystem::path DataDir(){
    wchar_t p[32768]{};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",p,_countof(p));
    return (n && n<_countof(p) ? std::filesystem::path(p) : std::filesystem::current_path())/L"TrayOrder";
}
bool Read(PCWSTR key,PCWSTR name,std::wstring& out){
    DWORD cb=0,type=0;
    if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,&type,nullptr,&cb)!=ERROR_SUCCESS||cb<2)return false;
    std::wstring s(cb/sizeof(wchar_t),L'\0');
    if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,&type,s.data(),&cb)!=ERROR_SUCCESS)return false;
    s.resize(wcslen(s.c_str())); out=std::move(s); return true;
}
bool Write(PCWSTR key,PCWSTR name,PCWSTR value){
    HKEY h{}; if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&h,nullptr)!=ERROR_SUCCESS)return false;
    LONG r=RegSetValueExW(h,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value),DWORD((wcslen(value)+1)*sizeof(wchar_t)));
    RegCloseKey(h); return r==ERROR_SUCCESS;
}
std::wstring Setting(PCWSTR name,PCWSTR def){
    std::wstring v; return Read(trayorder::kSettingsKey,name,v)?v:def;
}
void Defaults(){
    std::wstring v;
    if(!Read(trayorder::kSettingsKey,L"orderingBehavior",v))Write(trayorder::kSettingsKey,L"orderingBehavior",L"lockAll");
    if(!Read(trayorder::kSettingsKey,L"newIconBehavior",v))Write(trayorder::kSettingsKey,L"newIconBehavior",L"windowsDefault");
}
bool Startup(){
    std::wstring v; if(!Read(trayorder::kRunKey,trayorder::kRunValueName,v))return false;
    return _wcsicmp(v.c_str(),(L"\""+ExePath()+L"\" --startup").c_str())==0;
}
void SetStartup(bool on){
    if(on){ auto s=L"\""+ExePath()+L"\" --startup"; Write(trayorder::kRunKey,trayorder::kRunValueName,s.c_str()); return; }
    HKEY h{}; if(RegOpenKeyExW(HKEY_CURRENT_USER,trayorder::kRunKey,0,KEY_SET_VALUE,&h)==ERROR_SUCCESS){RegDeleteValueW(h,trayorder::kRunValueName);RegCloseKey(h);}
}
void CloseEvents(){
    if(g_ready)CloseHandle(g_ready); if(g_failed)CloseHandle(g_failed); if(g_stopped)CloseHandle(g_stopped);
    g_ready=g_failed=g_stopped=nullptr;
}
bool Command(trayorder::Command c,LPARAM data=0){
    return g_tid && PostThreadMessageW(g_tid,g_command,static_cast<WPARAM>(c),data);
}
void Detach(bool wait){
    if(g_hook){
        bool sent=Command(trayorder::Command::Shutdown);
        if(sent&&wait&&g_stopped)WaitForSingleObject(g_stopped,5000);
        UnhookWindowsHookEx(g_hook); g_hook=nullptr;
    }
    CloseEvents(); g_pid=g_tid=0;
}
void Tip(PCWSTR text){
    if(!g_window)return;
    NOTIFYICONDATAW n{}; n.cbSize=sizeof(n);n.hWnd=g_window;n.uID=1;n.guidItem=kTrayGuid;n.uFlags=NIF_TIP|NIF_GUID;
    wcsncpy_s(n.szTip,text,_TRUNCATE); Shell_NotifyIconW(NIM_MODIFY,&n);
}
bool LoadDll(){
    if(g_dll)return true;
    auto p=ExeDir()/L"TrayOrderHook.dll"; g_dll=LoadLibraryW(p.c_str());
    if(!g_dll)MessageBoxW(nullptr,L"TrayOrderHook.dll must be next to TrayOrder.exe.",L"TrayOrder",MB_ICONERROR);
    return g_dll!=nullptr;
}
bool Attach(){
    HWND tray=FindWindowW(L"Shell_TrayWnd",nullptr); DWORD pid=0; DWORD tid=tray?GetWindowThreadProcessId(tray,&pid):0;
    if(!pid||!tid){Tip(L"TrayOrder - waiting for Explorer");return false;}
    if(g_hook&&pid==g_pid&&tid==g_tid)return true;
    Detach(false); if(!LoadDll())return false;
    auto proc=reinterpret_cast<HOOKPROC>(GetProcAddress(g_dll,"TrayOrderHookProc")); if(!proc)return false;
    g_ready=CreateEventW(nullptr,TRUE,FALSE,trayorder::ReadyEventName(pid).c_str());
    g_failed=CreateEventW(nullptr,TRUE,FALSE,trayorder::FailedEventName(pid).c_str());
    g_stopped=CreateEventW(nullptr,TRUE,FALSE,trayorder::StoppedEventName(pid).c_str());
    g_hook=SetWindowsHookExW(WH_GETMESSAGE,proc,g_dll,tid);
    if(!g_hook){CloseEvents();Tip(L"TrayOrder - hook failed");return false;}
    g_pid=pid;g_tid=tid; Tip(L"TrayOrder - loading symbols");
    Command(trayorder::Command::Attach,GetCurrentProcessId()); PostThreadMessageW(tid,WM_NULL,0,0); return true;
}
void Poll(){
    HWND tray=FindWindowW(L"Shell_TrayWnd",nullptr); DWORD pid=0; DWORD tid=tray?GetWindowThreadProcessId(tray,&pid):0;
    if(!pid||!tid){if(g_hook)Detach(false);Tip(L"TrayOrder - waiting for Explorer");return;}
    if(!g_hook||pid!=g_pid||tid!=g_tid){Attach();return;}
    if(g_ready&&WaitForSingleObject(g_ready,0)==WAIT_OBJECT_0){
        bool edit=_wcsicmp(Setting(L"orderingBehavior",L"lockAll").c_str(),L"preserveManual")==0;
        Tip(edit?L"TrayOrder - reordering allowed":L"TrayOrder - order locked");
    }else if(g_failed&&WaitForSingleObject(g_failed,0)==WAIT_OBJECT_0)Tip(L"TrayOrder - hook failed (see log)");
}
void Reload(){ Command(trayorder::Command::ReloadSettings); PostThreadMessageW(g_tid,WM_NULL,0,0); }
void Baseline(){
    HKEY h{}; if(RegOpenKeyExW(HKEY_CURRENT_USER,trayorder::kStateKey,0,KEY_SET_VALUE,&h)==ERROR_SUCCESS){RegDeleteValueW(h,trayorder::kCanonicalValueName);RegCloseKey(h);}
    Reload();
}
void AddIcon(){
    NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=g_window;n.uID=1;n.guidItem=kTrayGuid;
    n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_GUID;n.uCallbackMessage=trayorder::kTrayCallbackMessage;
    n.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(n.szTip,L"TrayOrder - starting");
    Shell_NotifyIconW(NIM_ADD,&n);n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);
}
void DeleteIcon(){
    NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=g_window;n.uID=1;n.guidItem=kTrayGuid;n.uFlags=NIF_GUID;Shell_NotifyIconW(NIM_DELETE,&n);
}
void Menu(){
    HMENU m=CreatePopupMenu(); if(!m)return;
    bool edit=_wcsicmp(Setting(L"orderingBehavior",L"lockAll").c_str(),L"preserveManual")==0;
    bool end=_wcsicmp(Setting(L"newIconBehavior",L"windowsDefault").c_str(),L"placeAtEnd")==0;
    AppendMenuW(m,MF_STRING|(!edit?MF_CHECKED:0),M_LOCK,L"Lock saved order");
    AppendMenuW(m,MF_STRING|(edit?MF_CHECKED:0),M_EDIT,L"Allow reordering");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING|(!end?MF_CHECKED:0),M_DEFAULT,L"New icons: Windows default");
    AppendMenuW(m,MF_STRING|(end?MF_CHECKED:0),M_END,L"New icons: place at end");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,M_BASELINE,L"Use current order as baseline");
    AppendMenuW(m,MF_STRING|(Startup()?MF_CHECKED:0),M_STARTUP,L"Start with Windows");
    AppendMenuW(m,MF_STRING,M_LOG,L"Open log folder");
    AppendMenuW(m,MF_STRING,M_RELOAD,L"Reload Explorer hook");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,M_EXIT,L"Exit");
    POINT p{};GetCursorPos(&p);SetForegroundWindow(g_window);
    UINT c=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,g_window,nullptr);DestroyMenu(m);
    switch(c){
      case M_LOCK:Write(trayorder::kSettingsKey,L"orderingBehavior",L"lockAll");Reload();break;
      case M_EDIT:Write(trayorder::kSettingsKey,L"orderingBehavior",L"preserveManual");Reload();break;
      case M_DEFAULT:Write(trayorder::kSettingsKey,L"newIconBehavior",L"windowsDefault");Reload();break;
      case M_END:Write(trayorder::kSettingsKey,L"newIconBehavior",L"placeAtEnd");Reload();break;
      case M_BASELINE:Baseline();break;
      case M_STARTUP:SetStartup(!Startup());break;
      case M_LOG:{std::error_code e;std::filesystem::create_directories(DataDir(),e);ShellExecuteW(nullptr,L"open",DataDir().c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}
      case M_RELOAD:Detach(true);Attach();break;
      case M_EXIT:DestroyWindow(g_window);break;
    }
}
LRESULT CALLBACK WndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==trayorder::kTrayCallbackMessage){UINT e=LOWORD(lp);if(e==WM_CONTEXTMENU||e==WM_RBUTTONUP||e==NIN_SELECT||e==NIN_KEYSELECT){Menu();return 0;}}
    if(msg==WM_TIMER){Poll();return 0;}
    if(msg==WM_DESTROY){KillTimer(w,1);Detach(true);DeleteIcon();PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    g_singleton=CreateMutexW(nullptr,TRUE,L"Local\\TrayOrder.Controller.Singleton");
    if(!g_singleton||GetLastError()==ERROR_ALREADY_EXISTS)return 0;
    Defaults();g_command=RegisterWindowMessageW(trayorder::kCommandMessageName);
    WNDCLASSEXW c{};c.cbSize=sizeof(c);c.hInstance=h;c.lpfnWndProc=WndProc;c.lpszClassName=kClassName;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&c))return 1;
    g_window=CreateWindowExW(0,kClassName,L"TrayOrder",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,h,nullptr);if(!g_window)return 1;
    AddIcon();SetTimer(g_window,1,1000,nullptr);Attach();
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(g_dll)FreeLibrary(g_dll); if(g_singleton){ReleaseMutex(g_singleton);CloseHandle(g_singleton);} return int(m.wParam);
}