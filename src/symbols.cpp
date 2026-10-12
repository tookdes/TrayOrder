#include "windhawk_compat.hpp"
#include <dbghelp.h>
#include <winhttp.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Entry{std::wstring name;void* address{};};
struct Search{std::vector<Entry> v;size_t i{};};
struct Pdb{GUID guid{};DWORD age{};std::wstring name;};

std::mutex g_mutex;
HANDLE g_session{};
DWORD64 g_base{};
HMODULE g_module{};
std::vector<Entry> g_symbols;

void CleanupUnlocked(){
    if(g_session){if(g_base)SymUnloadModule64(g_session,g_base);SymCleanup(g_session);CloseHandle(g_session);}
    g_session=nullptr;g_base=0;g_module=nullptr;g_symbols.clear();
}

std::filesystem::path DataDir(){
    wchar_t p[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",p,_countof(p));
    return (n&&n<_countof(p)?std::filesystem::path(p):std::filesystem::current_path())/L"TrayOrder";
}
std::wstring Wide(std::string_view s){
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);if(n<=0)return {};
    std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;
}
bool PdbInfo(HMODULE mod,Pdb& out){
    ULONG bytes{};PIMAGE_SECTION_HEADER sec{};
    auto* dirs=static_cast<PIMAGE_DEBUG_DIRECTORY>(ImageDirectoryEntryToDataEx(mod,TRUE,IMAGE_DIRECTORY_ENTRY_DEBUG,&bytes,&sec));
    if(!dirs)return false;auto* base=reinterpret_cast<const BYTE*>(mod);
    for(size_t i=0;i<bytes/sizeof(IMAGE_DEBUG_DIRECTORY);++i){
        auto& d=dirs[i];if(d.Type!=IMAGE_DEBUG_TYPE_CODEVIEW||d.AddressOfRawData==0||d.SizeOfData<25)continue;
        const BYTE* p=base+d.AddressOfRawData;DWORD sig{};memcpy(&sig,p,4);if(sig!=0x53445352)continue;
        memcpy(&out.guid,p+4,sizeof(GUID));memcpy(&out.age,p+20,4);
        const char* z=reinterpret_cast<const char*>(p+24);size_t max=d.SizeOfData-24,n=0;while(n<max&&z[n])++n;if(!n||n==max)continue;
        out.name=std::filesystem::path(Wide({z,n})).filename().wstring();return !out.name.empty();
    }return false;
}
std::wstring Key(const Pdb& p){
    wchar_t b[80]{};auto& g=p.guid;
    swprintf_s(b,L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7],p.age);return b;
}
bool Download(PCWSTR object,const std::filesystem::path& dst){
    HINTERNET s=WinHttpOpen(L"TrayOrder/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!s)return false;
    HINTERNET c=WinHttpConnect(s,L"msdl.microsoft.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET r=c?WinHttpOpenRequest(c,L"GET",object,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    bool ok=r&&WinHttpSendRequest(r,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(r,nullptr);
    DWORD code{},cb=sizeof(code);if(ok)ok=WinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&cb,WINHTTP_NO_HEADER_INDEX)&&code==200;
    auto tmp=dst;tmp+=L".tmp";HANDLE f=ok?CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;ok=ok&&f!=INVALID_HANDLE_VALUE;
    std::vector<BYTE>b(65536);
    while(ok){DWORD avail{};if(!WinHttpQueryDataAvailable(r,&avail)){ok=false;break;}if(!avail)break;while(avail&&ok){DWORD got{};DWORD take=std::min<DWORD>(avail,DWORD(b.size()));if(!WinHttpReadData(r,b.data(),take,&got)){ok=false;break;}if(!got)break;DWORD wrote{};ok=WriteFile(f,b.data(),got,&wrote,nullptr)&&wrote==got;avail-=got;}}
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);if(r)WinHttpCloseHandle(r);if(c)WinHttpCloseHandle(c);WinHttpCloseHandle(s);
    if(!ok){DeleteFileW(tmp.c_str());return false;}return MoveFileExW(tmp.c_str(),dst.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
bool EnsurePdb(HMODULE mod,std::filesystem::path& dir){
    Pdb p;if(!PdbInfo(mod,p)){Wh_Log(L"SYMBOLS: no RSDS record");return false;}std::wstring key=Key(p);dir=DataDir()/L"symbols"/p.name/key;std::error_code ec;std::filesystem::create_directories(dir,ec);auto file=dir/p.name;
    if(!std::filesystem::exists(file,ec)||std::filesystem::file_size(file,ec)==0){std::wstring obj=L"/download/symbols/"+p.name+L"/"+key+L"/"+p.name;Wh_Log(L"SYMBOLS: downloading %s",obj.c_str());if(!Download(obj.c_str(),file)){Wh_Log(L"SYMBOLS: download failed");return false;}}
    return true;
}
BOOL CALLBACK EnumCb(PSYMBOL_INFOW p,ULONG,PVOID ctx){
    wchar_t u[8192]{};std::wstring n=UnDecorateSymbolNameW(p->Name,u,_countof(u),UNDNAME_COMPLETE)?u:p->Name;
    static_cast<std::vector<Entry>*>(ctx)->push_back({std::move(n),reinterpret_cast<void*>(p->Address)});return TRUE;
}
bool Ensure(HMODULE mod){
    std::lock_guard<std::mutex> l(g_mutex);if(g_session&&g_module==mod)return true;
    CleanupUnlocked();std::filesystem::path pdb;if(!EnsurePdb(mod,pdb))return false;
    wchar_t path[32768]{};if(!GetModuleFileNameW(mod,path,_countof(path)))return false;
    g_session=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!g_session)return false;
    SymSetOptions((SymGetOptions()|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS)&~SYMOPT_UNDNAME);
    if(!SymInitializeW(g_session,pdb.c_str(),FALSE)){CloseHandle(g_session);g_session=nullptr;return false;}
    g_base=SymLoadModuleExW(g_session,nullptr,path,L"taskbar",reinterpret_cast<DWORD64>(mod),0,nullptr,0);
    if(!g_base||!SymEnumSymbolsW(g_session,g_base,nullptr,EnumCb,&g_symbols)){Wh_Log(L"SYMBOLS: DbgHelp load/enumerate failed %lu",GetLastError());CleanupUnlocked();return false;}
    g_module=mod;Wh_Log(L"SYMBOLS: loaded %llu symbols",static_cast<unsigned long long>(g_symbols.size()));return true;
}
std::wstring Normalize(std::wstring s){
    constexpr PCWSTR tokens[]={L"public:",L"private:",L"protected:",L"virtual",L"static",L"__cdecl",L"__thiscall",L"struct",L"class",L"enum",L"const"};
    for(auto t:tokens)for(size_t p;(p=s.find(t))!=std::wstring::npos;)s.erase(p,wcslen(t));
    s.erase(std::remove_if(s.begin(),s.end(),[](wchar_t c){return iswspace(c)!=0;}),s.end());return s;
}
bool Match(std::wstring_view a,std::wstring_view e){
    if(a==e||Normalize(std::wstring(a))==Normalize(std::wstring(e)))return true;
    auto open=e.find(L'('),scope=e.rfind(L"::",open);if(open==std::wstring_view::npos||scope==std::wstring_view::npos)return false;
    std::wstring leaf(e.substr(scope+2,open-scope-2));return !leaf.empty()&&a.find(leaf+L"(")!=std::wstring_view::npos;
}
void* Resolve(HMODULE m,PCWSTR candidate){
    if(!Ensure(m))return nullptr;void* hit{};size_t count{};
    for(auto& s:g_symbols)if(Match(s.name,candidate)){hit=s.address;++count;}
    if(count!=1){Wh_Log(L"SYMBOLS: %llu matches for %s",static_cast<unsigned long long>(count),candidate);return nullptr;}return hit;
}
}

HANDLE Wh_FindFirstSymbol(HMODULE m,const WH_FIND_SYMBOL_OPTIONS*,WH_FIND_SYMBOL* d){
    if(!d||!Ensure(m))return nullptr;auto* s=new(std::nothrow)Search;if(!s)return nullptr;s->v=g_symbols;if(s->v.empty()){delete s;return nullptr;}d->symbol=s->v[0].name.c_str();d->address=s->v[0].address;return s;
}
BOOL Wh_FindNextSymbol(HANDLE h,WH_FIND_SYMBOL* d){if(!h||!d)return FALSE;auto* s=static_cast<Search*>(h);if(++s->i>=s->v.size())return FALSE;d->symbol=s->v[s->i].name.c_str();d->address=s->v[s->i].address;return TRUE;}
void Wh_FindCloseSymbol(HANDLE h){delete static_cast<Search*>(h);}

BOOL WindhawkUtils::HookSymbols(HMODULE m,SYMBOL_HOOK* hooks,std::size_t n){
    for(size_t i=0;i<n;++i){void* a{};for(size_t j=0;j<hooks[i].symbolsCount&&!a;++j)a=Resolve(m,hooks[i].symbols[j]);
        if(!a){if(hooks[i].optional)continue;Wh_Log(L"SYMBOLS: required hook %llu missing",static_cast<unsigned long long>(i));return FALSE;}
        if(!CompatSetFunctionHook(a,hooks[i].pHookFunction,hooks[i].ppOriginalFunction))return FALSE;
    }return TRUE;
}
void TrayOrderSymbolCleanup(){std::lock_guard<std::mutex> l(g_mutex);CleanupUnlocked();}