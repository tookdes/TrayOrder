#include "windhawk_compat.hpp"
#include "interop.hpp"
#include <minhook.h>
#include <atomic>
#include <cstdarg>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::mutex g_hooksMutex,g_logMutex;
std::vector<void*> g_pending,g_all;
std::atomic<bool> g_mh{};
std::filesystem::path DataDir(){
    wchar_t p[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",p,_countof(p));
    return (n&&n<_countof(p)?std::filesystem::path(p):std::filesystem::current_path())/L"TrayOrder";
}
bool Read(PCWSTR key,PCWSTR name,std::wstring& out){
    DWORD cb=0,type=0;if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,&type,nullptr,&cb)!=ERROR_SUCCESS||cb<2)return false;
    std::wstring s(cb/sizeof(wchar_t),L'\0');if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,&type,s.data(),&cb)!=ERROR_SUCCESS)return false;
    s.resize(wcslen(s.c_str()));out=std::move(s);return true;
}
bool Write(PCWSTR key,PCWSTR name,PCWSTR value){
    HKEY h{};if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&h,nullptr)!=ERROR_SUCCESS)return false;
    LONG r=RegSetValueExW(h,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value),DWORD((wcslen(value)+1)*sizeof(wchar_t)));RegCloseKey(h);return r==ERROR_SUCCESS;
}
}

void Wh_Log(PCWSTR format,...){
    wchar_t msg[8192]{};va_list a;va_start(a,format);_vsnwprintf_s(msg,_countof(msg),_TRUNCATE,format,a);va_end(a);
    SYSTEMTIME t{};GetLocalTime(&t);wchar_t line[9000]{};
    swprintf_s(line,L"%04u-%02u-%02u %02u:%02u:%02u [%lu] %s\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),msg);
    OutputDebugStringW(line);
    std::lock_guard<std::mutex> l(g_logMutex);std::error_code ec;std::filesystem::create_directories(DataDir(),ec);
    auto p=DataDir()/L"TrayOrder.log";HANDLE h=CreateFileW(p.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    int n=WideCharToMultiByte(CP_UTF8,0,line,-1,nullptr,0,nullptr,nullptr);if(n>1){std::vector<char>b(n);WideCharToMultiByte(CP_UTF8,0,line,-1,b.data(),n,nullptr,nullptr);DWORD w{};WriteFile(h,b.data(),n-1,&w,nullptr);}CloseHandle(h);
}
std::size_t Wh_GetStringValue(PCWSTR name,PWSTR buffer,std::size_t chars){
    if(!name||!buffer||!chars)return 0;std::wstring v;if(!Read(trayorder::kStateKey,name,v)){buffer[0]=0;return 0;}
    if(v.size()+1>chars){buffer[0]=0;SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}wcscpy_s(buffer,chars,v.c_str());return v.size();
}
BOOL Wh_SetStringValue(PCWSTR name,PCWSTR value){return name&&value&&Write(trayorder::kStateKey,name,value);}
PCWSTR Wh_GetStringSetting(PCWSTR name){
    if(!name)return nullptr;std::wstring v;
    if(!Read(trayorder::kSettingsKey,name,v)){
        if(_wcsicmp(name,L"orderingBehavior")==0)v=L"lockAll";
        else if(_wcsicmp(name,L"newIconBehavior")==0)v=L"windowsDefault";
    }
    auto* p=static_cast<wchar_t*>(CoTaskMemAlloc((v.size()+1)*sizeof(wchar_t)));if(!p)return nullptr;wcscpy_s(p,v.size()+1,v.c_str());return p;
}
void Wh_FreeStringSetting(PCWSTR s){CoTaskMemFree(const_cast<PWSTR>(s));}

bool WindhawkUtils::CompatSetFunctionHook(void* target,void* hook,void** original){
    if(!target||!hook||!original)return false;std::lock_guard<std::mutex> l(g_hooksMutex);
    MH_STATUS s=MH_CreateHook(target,hook,original);if(s!=MH_OK){Wh_Log(L"HOOK: create failed %S",MH_StatusToString(s));return false;}
    g_pending.push_back(target);g_all.push_back(target);return true;
}
BOOL Wh_ApplyHookOperations(){
    std::vector<void*> p;{std::lock_guard<std::mutex> l(g_hooksMutex);p.swap(g_pending);}
    for(void* t:p){MH_STATUS s=MH_EnableHook(t);if(s!=MH_OK&&s!=MH_ERROR_ENABLED){Wh_Log(L"HOOK: enable failed %S",MH_StatusToString(s));return FALSE;}}return TRUE;
}
bool TrayOrderCompatInitialize(){
    std::error_code ec;std::filesystem::create_directories(DataDir(),ec);
    MH_STATUS s=MH_Initialize();if(s!=MH_OK&&s!=MH_ERROR_ALREADY_INITIALIZED){Wh_Log(L"HOOK: initialize failed %S",MH_StatusToString(s));return false;}g_mh=true;return true;
}
void TrayOrderCompatDisableHooks(){
    if(!g_mh)return;std::lock_guard<std::mutex> l(g_hooksMutex);
    for(auto it=g_all.rbegin();it!=g_all.rend();++it){MH_STATUS s=MH_DisableHook(*it);if(s!=MH_OK&&s!=MH_ERROR_DISABLED&&s!=MH_ERROR_NOT_CREATED)Wh_Log(L"HOOK: disable failed %S",MH_StatusToString(s));}
}
void TrayOrderCompatCleanup(){
    if(g_mh.exchange(false)){std::lock_guard<std::mutex> l(g_hooksMutex);for(void* t:g_all)MH_RemoveHook(t);g_all.clear();g_pending.clear();MH_Uninitialize();}
    TrayOrderSymbolCleanup();
}