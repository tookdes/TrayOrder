#include <windows.h>
#include <atomic>
#include <mutex>
#include "interop.hpp"
#include "windhawk_compat.hpp"

BOOL Wh_ModInit();
void Wh_ModAfterInit();
void Wh_ModSettingsChanged();
void Wh_ModUninit();

namespace {
std::once_flag g_once;
std::atomic<bool> g_ready{},g_done{},g_stopping{};
std::atomic<DWORD> g_controller{};
HANDLE g_controllerHandle{};
HMODULE g_self{};

UINT CommandMessage(){static UINT v=RegisterWindowMessageW(trayorder::kCommandMessageName);return v;}
void Signal(const std::wstring& name){HANDLE e=CreateEventW(nullptr,TRUE,FALSE,name.c_str());if(e){SetEvent(e);CloseHandle(e);}}

DWORD WINAPI StopThread(void*){
    if(g_stopping.exchange(true))return 0;
    TrayOrderCompatDisableHooks();
    if(g_ready.exchange(false))Wh_ModUninit();
    TrayOrderCompatCleanup();
    if(g_controllerHandle){CloseHandle(g_controllerHandle);g_controllerHandle=nullptr;}
    Signal(trayorder::StoppedEventName(GetCurrentProcessId()));
    HMODULE self=g_self;g_self=nullptr;
    if(self)FreeLibraryAndExitThread(self,0);
    return 0;
}
void Stop(){
    if(g_stopping.load())return;
    HANDLE t=CreateThread(nullptr,0,StopThread,nullptr,0,nullptr);if(t)CloseHandle(t);
}
DWORD WINAPI WatchThread(void*){
    HANDLE h=g_controllerHandle;if(h){WaitForSingleObject(h,INFINITE);Stop();}return 0;
}
void Watch(){
    if(g_controllerHandle)return;
    DWORD pid=g_controller.load();if(!pid)return;
    g_controllerHandle=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!g_controllerHandle)return;
    HANDLE t=CreateThread(nullptr,0,WatchThread,nullptr,0,nullptr);if(t)CloseHandle(t);
}
DWORD WINAPI InitThread(void*){
    HMODULE ref{};
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&InitThread),&ref))g_self=ref;
    bool ok=TrayOrderCompatInitialize();
    if(ok)ok=Wh_ModInit()!=FALSE;
    if(ok)ok=Wh_ApplyHookOperations()!=FALSE;
    if(ok){
        Wh_ModAfterInit();g_ready.store(true);Wh_Log(L"BOOTSTRAP: standalone hook ready");
        Signal(trayorder::ReadyEventName(GetCurrentProcessId()));Watch();
    }else{
        Wh_Log(L"BOOTSTRAP: initialization failed");TrayOrderCompatDisableHooks();TrayOrderCompatCleanup();
        Signal(trayorder::FailedEventName(GetCurrentProcessId()));
    }
    g_done.store(true);return 0;
}
void Init(){
    std::call_once(g_once,[]{HANDLE t=CreateThread(nullptr,0,InitThread,nullptr,0,nullptr);if(t)CloseHandle(t);else Signal(trayorder::FailedEventName(GetCurrentProcessId()));});
}
}

extern "C" __declspec(dllexport) LRESULT CALLBACK TrayOrderHookProc(int code,WPARAM wp,LPARAM lp){
    if(code>=0){
        auto* m=reinterpret_cast<MSG*>(lp);
        if(m&&m->message==CommandMessage()){
            auto c=static_cast<trayorder::Command>(m->wParam);
            if(c==trayorder::Command::Attach){g_controller.store(DWORD(m->lParam));Init();if(g_done.load())Watch();}
            else if(c==trayorder::Command::ReloadSettings){if(g_ready.load()&&!g_stopping.load())Wh_ModSettingsChanged();}
            else if(c==trayorder::Command::Shutdown)Stop();
        }else Init();
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);
    return TRUE;
}