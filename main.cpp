#include <iostream>
#include <memory>
#include <functional>
#include <thread>
#include <chrono>
#include <windows.h>
#include "psapi.h"
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <atomic>

enum class Keys {
    CAPSLOCK = VK_CAPITAL,
    PGUP = VK_PRIOR,
    R = 0x52
};
enum class Mouse {
    HOLD,
    RELEASE,
    RIGHT_RELESE = WM_RBUTTONUP,
    RIGHT_HOLD = WM_RBUTTONDOWN
};
enum class ReturnCode {
    ADDRESS = 0x1,
    VALUE = 0x0
};

namespace global {
    inline MSG msg{};

    inline KBDLLHOOKSTRUCT kbrdStruct{};
    inline HHOOK kbrdHook{};

    inline MOUSEHOOKSTRUCT mouseStruct{};
    inline HHOOK mouseHook{};
}
namespace flag {
    inline std::atomic<bool> triggerActive{ false }; // trigger bot is active
    inline std::atomic<bool> terminate{ false }; // terminate the program 
    inline std::atomic<bool> holdMouseRight{ false }; // user is holding the right mouse button
    inline std::atomic<bool> shouldFire{ false }; // the trigger bot should fire
}
namespace game {
    inline LPCVOID noEnemeyHover{ (LPCVOID)0xFFFFFFFF }; // aims to no enemy
    inline constexpr LPCVOID noFriendHover{ 0x0 }; // aims to no friend
}
namespace constants {
    inline constexpr LPCSTR windowName{ "Alien Swarm: Reactive Drop" }; // window name
    inline constexpr LPCSTR moduleName{ "reactivedrop.exe" }; // and its module
    inline constexpr LPCSTR procName{ "client.dll" };
    inline constexpr uint_fast32_t checkInterval{ 160 }; // global while(!terminate) sleep interval in ms for all intervals
    inline constexpr DWORD mouseDelay{ 100 }; // delay before mosue input
    inline constexpr DWORD keyboardDelay{ 100 }; // delay before key input
    inline constexpr HWND consoleHWND{ NULL }; // console handle
}

constexpr auto findGameWindow{ [](LPCSTR windowName) constexpr->HWND {
    return FindWindowEx(NULL, 0, 0, windowName);
} };

constexpr auto getWindowProcessId{ [](const HWND gameWindow)  constexpr->DWORD {
    DWORD winProcId { 0x0 };
    if (!gameWindow) {
        std::cout << "Game window Not found\n";
        return winProcId;
    }
    GetWindowThreadProcessId(gameWindow, &winProcId);
    return winProcId;
} };

constexpr auto openWindowProcessId{ [](const DWORD winProcId) constexpr -> HANDLE {
    if (!winProcId) {
        std::cout << "Failed to get process id\n";
        return static_cast<HANDLE>(0);
    }
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, winProcId);
} };

constexpr auto getModuleEntry{ [](const DWORD proc, std::string modName) constexpr -> MODULEENTRY32 {
    HANDLE hSnap { CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, proc) };
    MODULEENTRY32 modEntry  {};
    const auto CLEAN_RETURN {[&hSnap,&modEntry]() constexpr {
        CloseHandle(hSnap);
        return modEntry;
    }};
    if (hSnap == INVALID_HANDLE_VALUE) {
        return CLEAN_RETURN();
    }
    modEntry.dwSize = sizeof(modEntry);
    if (!Module32First(hSnap, &modEntry)) {
        return CLEAN_RETURN();
    }
    while (modName.compare(modEntry.szModule) != 0) {
        Module32Next(hSnap, &modEntry);
    }
    return CLEAN_RETURN();
} };

constexpr auto writeMemory{ [](const HANDLE window,const LPVOID address,const  DWORD value) constexpr ->void {
    // WriteProcessMemory(window, (BYTE *)address, &value, sizeof(&value), NULL);
    WriteProcessMemory(window, address, &value, sizeof(&value), NULL);
} };

constexpr auto readMemory{ [](const HANDLE phandle, const DWORD_PTR baseAddress, const std::vector<DWORD> offsets,const ReturnCode code) constexpr->LPVOID {
    LPVOID address_PTR { (LPVOID)baseAddress };
    DWORD64 temp { 0x0 };
    ReadProcessMemory(phandle, address_PTR, &temp, sizeof(DWORD32), NULL);
    // std::cout << std::hex << address_PTR << "-" << temp << std::endl;
    for (uint_fast8_t i {0}; i < offsets.size(); ++i) {
        address_PTR = (LPVOID)(temp + offsets[i]);
        ReadProcessMemory(phandle, address_PTR, &temp, sizeof(DWORD32), NULL);
        // std::cout << std::hex << address_PTR << "-" << temp << std::endl;
    }
    switch (code) {
    case ReturnCode::ADDRESS:
        return address_PTR;
    case ReturnCode::VALUE:
        return (LPVOID)temp;
    default:
        return (LPVOID)0x0;
    }
} };

constexpr auto setInterval{ [](
    const std::function<void(void)> func,
    const std::function<uint_least32_t(void)> interval,
    const std::function<bool(void)> condition) {

        auto th = std::thread([func, interval, condition]() {
            do {
                if (std::invoke(condition)) {
                    std::invoke(func);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(std::invoke(interval)));
            } while (!flag::terminate.load(std::memory_order_relaxed));
        });
        th.detach();
        return th;
} };

constexpr auto sendKey{ [](const HWND windowName,UINT msg,WPARAM vkCode) constexpr->void {
    std::this_thread::sleep_for(std::chrono::milliseconds(constants::keyboardDelay));
    PostMessage(windowName,msg,vkCode,MAPVK_VSC_TO_VK);
} };

constexpr auto sendClick{ [](const HWND windowName,UINT msg,WPARAM vkCode) constexpr->void {
    // sleep before clicking
    // std::this_thread::sleep_for(std::chrono::milliseconds(constants::mouseDelay));
    PostMessage(windowName,msg,vkCode,MAPVK_VSC_TO_VK);
    // PostMessage(windowName,WM_LBUTTONUP,vkCode,MAPVK_VSC_TO_VK);
} };

constexpr auto captureKeyPress{ [](const MSG& msg)constexpr ->void {
    switch ((Keys)msg.wParam) {
        case Keys::CAPSLOCK: // toggle
            if (msg.message == WM_KEYUP) {
                flag::triggerActive.store(!flag::triggerActive.load(std::memory_order_relaxed),std::memory_order_relaxed);
                flag::shouldFire.store(flag::triggerActive.load(std::memory_order_relaxed) ? flag::shouldFire.load(std::memory_order_relaxed) : false);
                // std::cout << (flag::triggerActive ? " active" : "deactive") << "\n";
            }
        break;
        case Keys::PGUP:
            flag::terminate.store(true,std::memory_order_relaxed);
        break;
        default:
        break;
    }
} };

constexpr auto captureMousePress{ [](const MSG& msg)constexpr ->void {
    switch ((Mouse)msg.wParam) {
        case Mouse::RIGHT_HOLD:
            flag::holdMouseRight.store(true,std::memory_order_relaxed);
        break;
        case Mouse::RIGHT_RELESE:
            flag::holdMouseRight.store(false,std::memory_order_relaxed);
        break;
        default:
        break;
    }
} };

constexpr auto lowLevelKeyboardProc{ [] (int nCode, WPARAM wParam, LPARAM lParam) constexpr->LRESULT __stdcall {
       if (nCode != HC_ACTION)
           return CallNextHookEx(global::kbrdHook, nCode, wParam, lParam);
       if (wParam == WM_KEYUP || wParam == WM_KEYDOWN) {
           global::kbrdStruct = (*(KBDLLHOOKSTRUCT*)lParam);
           PostMessage(constants::consoleHWND,(UINT)wParam,global::kbrdStruct.vkCode,MAPVK_VSC_TO_VK);
       }
       return CallNextHookEx(global::kbrdHook, nCode, wParam, lParam);
   } };

constexpr auto lowLevelMouseProc{ [] (int nCode,WPARAM wParam,LPARAM lParam)constexpr->LRESULT __stdcall {
    if (nCode != HC_ACTION)
        return CallNextHookEx(global::mouseHook,nCode,wParam,lParam);
        if (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP) {
            PostMessage(constants::consoleHWND,(UINT)wParam,wParam,MAPVK_VSC_TO_VK);
        }

    return CallNextHookEx(global::mouseHook,nCode,wParam,lParam);
} };

int main() {

    // get window data HWND/ID/HANDLE

    const std::atomic<HWND> gamewindow{ findGameWindow(constants::windowName) };
    const auto winProcId{ gamewindow.load(std::memory_order_relaxed) ? getWindowProcessId(gamewindow.load(std::memory_order_relaxed)) : 0x0 };
    std::atomic<HANDLE> phandle{ winProcId ? openWindowProcessId(winProcId) : 0x0 };
    const auto gameHandle{ GetModuleHandle(constants::moduleName) };

    // used for cleaning side-effects as program progress should be re-initialized after each side effect
    std::function<int(int)> mainReturn{ [&phandle](int exitCode) constexpr -> int {
        CloseHandle(phandle.exchange(nullptr,std::memory_order_relaxed));
        return exitCode;
    } };
    // the window specified are not there
    if (!winProcId || !gamewindow.load(std::memory_order_relaxed) || !phandle.load(std::memory_order_relaxed)) {
        std::cout << constants::windowName << " Game not found \n";
        return mainReturn(EXIT_FAILURE);
    }
    // current module and thier threads ids
    const auto modEntry{ getModuleEntry(winProcId, constants::moduleName) };
    const auto procEntry{ getModuleEntry(winProcId,constants::procName) };
    const auto modEntryThreadId = GetThreadId(modEntry.hModule);
    const auto procEntryThreadId = GetThreadId(procEntry.hModule);

    // loggs << should be edited or make logger

    std::cout << "Game: " << constants::windowName <<
        "\nWindow HWND: " << gamewindow.load(std::memory_order_relaxed) <<
        "\nProcessId: " << winProcId <<
        "\nHandle: " << phandle.load(std::memory_order_relaxed) <<
        "\nmodBaseAddress: " << std::hex << (DWORD_PTR)modEntry.modBaseAddr <<
        "\nMemory Check Interval: " << constants::checkInterval <<
        "\nMouse Interval: " << constants::mouseDelay <<
        "\nKeyboard Interval: " << constants::keyboardDelay <<
        "\n---Game is Running---\n";



    // calculate the address  based on entry base address

    // const auto enemeyHoverAdress{ readMemory(phandle, baseAddress + 0x0147E1C0, { 0x23C, 0x138, 0x74, 0x74, 0x20 }, ReturnCode::ADDRESS) }; // F.E.A.R. 3

    // constant address
    const auto enemeyHoverAdress{ (DWORD_PTR)readMemory(phandle.load(std::memory_order_relaxed), (DWORD_PTR)procEntry.modBaseAddr + 0x84A3E0, { }, ReturnCode::ADDRESS) }; //Alien Swarm: Reactive Drop


    // auto enemeyHoverAdress = readMemory(phandle, modBaseAddress + 0x2FA5D4, { }, ReturnCode::ADDRESS); //cod4

    // auto enemeyHoverAdress = readMemory(phandle, modBaseAddress + 0x1F46790, { }, ReturnCode::ADDRESS); // gta5

    // writeMemory(phandle, value, 0x42CA0000); has issue with data type conversion dicimal to float

    if (!(global::kbrdHook = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardProc, gameHandle, modEntryThreadId))) {
        std::cout << "Failed to install keybord Hook! \n";
        return mainReturn(EXIT_FAILURE);
    } else {
        mainReturn = [mainReturn](int exitCode)constexpr->int {
            UnhookWindowsHookEx(global::kbrdHook);
            return mainReturn(exitCode);
        };
    }

    if (!(global::mouseHook = SetWindowsHookEx(WH_MOUSE_LL, lowLevelMouseProc, gameHandle, modEntryThreadId))) {
        std::cout << "Failed to install mouse Hook! \n";
        return mainReturn(EXIT_FAILURE);
    } else {
        mainReturn = [mainReturn](int exitCode)constexpr->int {
            UnhookWindowsHookEx(global::mouseHook);
            return mainReturn(exitCode);
        };
    }

    std::cout << "Trigger bot is activated (aim to enemies to auto shoot)\nPGUP to Close\n";

    // intervals callback

    const auto getEnemeyHover{ [
            enemyHover{(LPVOID)0x0},
            friendHover{(LPVOID)0x0},
            &phandle,
            &procEntry,
            enemeyHoverAdress
        ] () mutable ->void{
        enemyHover = readMemory(phandle.load(std::memory_order_relaxed), enemeyHoverAdress , { }, ReturnCode::VALUE);

        if (enemyHover == game::noEnemeyHover) {
             flag::shouldFire.store(false,std::memory_order_relaxed);
             return;
        }
        //  this pointer address is being recalculated each time you change game / character
         friendHover = readMemory(phandle.load(std::memory_order_relaxed),(DWORD_PTR)procEntry.modBaseAddr + 0x0088DC14,{0x0,0xA88,0,0xc0,0xfb0},ReturnCode::VALUE);

        if (friendHover != game::noFriendHover) {
            flag::shouldFire.store(false,std::memory_order_relaxed);
            return;
        }
        flag::shouldFire.store(flag::triggerActive.load(std::memory_order_relaxed),std::memory_order_relaxed);

  } };

    // run on separate thread
    auto hoverInterval = setInterval(getEnemeyHover,
        []()constexpr->uint_fast32_t {return constants::checkInterval;},
        []()constexpr->bool { return flag::triggerActive.load(std::memory_order_relaxed) && !flag::terminate.load(std::memory_order_relaxed); }
    );

    if (!hoverInterval.joinable()) {
        mainReturn = [&hoverInterval, mainReturn](int exitCode)constexpr->int {
            hoverInterval.~thread();
            return mainReturn(exitCode);
        };
    } else {
        std::cout << "couldn't attach thread to check on enemy\n";
        return mainReturn(EXIT_FAILURE);
    }

    auto clickInterval = setInterval(
        [&gamewindow]()constexpr->void {
        std::thread([&gamewindow]()constexpr->void {
            sendClick(gamewindow.load(std::memory_order_relaxed), WM_LBUTTONDOWN, VK_LBUTTON);
        }).detach();
    },
        []()constexpr-> uint_fast32_t { return flag::triggerActive.load(std::memory_order_relaxed) ? constants::mouseDelay : constants::checkInterval;},
        []()constexpr->bool { return flag::shouldFire.load(std::memory_order_relaxed) && !flag::terminate.load(std::memory_order_relaxed); }
    );

    if (!clickInterval.joinable()) {
        mainReturn = [&clickInterval, mainReturn](int exitCode)constexpr->int {
            clickInterval.~thread();
            return mainReturn(exitCode);
        };
    } else {
        std::cout << "couldn't attach thread to check on click\n";
        return mainReturn(EXIT_FAILURE);
    }

    // setInterval(getAmmo, constants::checkInterval, []()constexpr->bool { return flag::triggerActive;});

    // exit program on page up
    while ((GetMessage(&global::msg, constants::consoleHWND, 0, 0) != 0) && !flag::terminate.load(std::memory_order_relaxed)) {

        // if (global::msg.message == WM_KEYUP || global::msg.message == WM_KEYDOWN)
        std::thread([]()constexpr->void {captureKeyPress(global::msg);}).detach();

        std::thread([]()constexpr->void {captureMousePress(global::msg);}).detach();
    }

    std::cout << "Closed\n";
    return mainReturn(EXIT_SUCCESS);
}