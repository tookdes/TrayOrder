#pragma once

#include <windows.h>
#include <cstddef>
#include <initializer_list>

struct WH_FIND_SYMBOL_OPTIONS {
    std::size_t optionsSize = sizeof(WH_FIND_SYMBOL_OPTIONS);
    PCWSTR symbolServer = nullptr;
    BOOL noUndecoratedSymbols = FALSE;
};

struct WH_FIND_SYMBOL {
    PCWSTR symbol = nullptr;
    void* address = nullptr;
};

void Wh_Log(PCWSTR format, ...);
std::size_t Wh_GetStringValue(PCWSTR valueName, PWSTR stringBuffer, std::size_t bufferChars);
BOOL Wh_SetStringValue(PCWSTR valueName, PCWSTR value);
PCWSTR Wh_GetStringSetting(PCWSTR valueName);
void Wh_FreeStringSetting(PCWSTR string);
BOOL Wh_ApplyHookOperations();
HANDLE Wh_FindFirstSymbol(HMODULE module, const WH_FIND_SYMBOL_OPTIONS* options, WH_FIND_SYMBOL* findData);
BOOL Wh_FindNextSymbol(HANDLE symSearch, WH_FIND_SYMBOL* findData);
void Wh_FindCloseSymbol(HANDLE symSearch);

bool TrayOrderCompatInitialize();
void TrayOrderCompatDisableHooks();
void TrayOrderCompatCleanup();
void TrayOrderSymbolCleanup();

namespace WindhawkUtils {

struct SYMBOL_HOOK {
    PCWSTR symbols[10]{};
    std::size_t symbolsCount = 0;
    void** ppOriginalFunction = nullptr;
    void* pHookFunction = nullptr;
    bool optional = false;

    template <typename TOriginal, typename THook>
    SYMBOL_HOOK(std::initializer_list<PCWSTR> candidates,
                TOriginal* original,
                THook hook,
                bool isOptional = false)
        : ppOriginalFunction(reinterpret_cast<void**>(original)),
          pHookFunction(reinterpret_cast<void*>(hook)),
          optional(isOptional) {
        for (PCWSTR candidate : candidates) {
            if (symbolsCount >= _countof(symbols)) {
                break;
            }
            symbols[symbolsCount++] = candidate;
        }
    }
};

bool CompatSetFunctionHook(void* target, void* hook, void** original);

template <typename TTarget, typename THook, typename TOriginal>
bool SetFunctionHook(TTarget target, THook hook, TOriginal* original) {
    return CompatSetFunctionHook(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(hook),
        reinterpret_cast<void**>(original));
}

BOOL HookSymbols(HMODULE module, SYMBOL_HOOK* hooks, std::size_t hookCount);

}  // namespace WindhawkUtils
