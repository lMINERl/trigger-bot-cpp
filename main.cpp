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

enum class Keys {
    CAPSLOCK = VK_CAPITAL,
    PGUP = VK_PRIOR,
    R = 0x52
};
enum class Mouse {
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
    inline bool triggerActive{ false };
    inline bool terminate{ false };
    inline bool holdMouseRight{ false };
}
namespace constants {
    inline constexpr LPCSTR windowName{ "Alien Swarm: Reactive Drop" };
    inline constexpr LPCSTR moduleName{ "reactivedrop.exe" };
    inline constexpr LPCSTR procName{ "client.dll" };
    inline constexpr uint_fast32_t checkInterval{ 80 };
    inline constexpr DWORD mouseDelay{ 97 };
    inline constexpr DWORD keyboardDelay{ 100 };
}

const auto findGameWindow{ [](LPCSTR windowName) constexpr->HWND {
    return FindWindowEx(NULL, 0, 0, windowName);
} };
const auto getWindowProcessId{ [](const HWND gameWindow)  constexpr->DWORD {
    DWORD winProcId { 0x0 };
    if (!gameWindow) {
        std::cout << "Game window Not found\n";
        return winProcId;
    }
    GetWindowThreadProcessId(gameWindow, &winProcId);
    return winProcId;
} };
const auto openWindowProcessId{ [](const DWORD winProcId) constexpr -> HANDLE {
    if (!winProcId) {
        std::cout << "Failed to get process id\n";
        return static_cast<HANDLE>(0);
    }
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, winProcId);
} };

const auto getModuleEntry{ [](const DWORD proc, std::string modName) constexpr -> MODULEENTRY32 {
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

const auto writeMemory{ [](const HANDLE window,const LPVOID address,const  DWORD value) constexpr ->void {
    // WriteProcessMemory(window, (BYTE *)address, &value, sizeof(&value), NULL);
    WriteProcessMemory(window, address, &value, sizeof(&value), NULL);
} };
const auto readMemory{ [](const HANDLE phandle, const DWORD_PTR baseAddress, const std::vector<DWORD> offsets,const ReturnCode code) constexpr->LPVOID {
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

const auto setInterval{ [](const std::function<void(void)> func, const uint_fast32_t interval, const std::function<bool(void)> condition) constexpr ->void {
    std::thread([func, interval, condition]() {
        do {
            while (condition()) {
                func();
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        } while (true);
    }).detach();
} };

const auto sendKey{ [](WORD key, INPUT kyBrd[2], unsigned int repeat,const DWORD delay) constexpr->void {

    PostMessage(findGameWindow(constants::windowName),WM_KEYDOWN,(WPARAM)Keys::R,1);

    keybd_event(0,(BYTE)Keys::R,KEYEVENTF_EXTENDEDKEY | 0,0);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    keybd_event(0,(BYTE)Keys::R,KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP,0);

    kyBrd[0].type = kyBrd[1].type = INPUT_KEYBOARD;
    kyBrd[0].ki.time = 0;
    kyBrd[0].ki.dwExtraInfo = 0;
    kyBrd[0].ki.dwFlags = 0;
    kyBrd[0].ki.wVk = key;


    do {
        std::cout << key << std::endl;
        SendInput(1, &kyBrd[0], sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        kyBrd[0].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &kyBrd[0], sizeof(INPUT));
        --repeat;
    } while (repeat > 0);
} };
const auto sendClick{ [](INPUT mouse[2], unsigned int repeat,const DWORD delay) constexpr->void {
    mouse[0].type = mouse[1].type = INPUT_MOUSE;
    mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    do {
        SendInput(1, &mouse[0], sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        SendInput(1, &mouse[1], sizeof(INPUT));
        --repeat;
    } while (repeat > 0);
} };

const auto captureKeyPress{ [](WPARAM wParam,LPARAM vkCode)constexpr ->void {
    switch ((Keys)vkCode) {
        case Keys::CAPSLOCK: // toggle
            if (wParam == WM_KEYUP) {
                flag::triggerActive = !flag::triggerActive;
                // std::cout << (flag::triggerActive ? " active" : "deactive") << "\n";
            }
        break;
        case Keys::PGUP:
            flag::terminate = true;
        break;
        default:
        break;
    }

} };
const auto captureMousePress{ [](DWORD wm_mButton)constexpr->void {
    switch ((Mouse)wm_mButton) {
        case Mouse::RIGHT_HOLD:
            flag::holdMouseRight = true;
        break;
        case Mouse::RIGHT_RELESE:
            flag::holdMouseRight = false;
        break;
        default:
        break;
    }
} };

const auto lowLevelKeyboardProc{ [](int nCode, WPARAM wParam, LPARAM lParam) constexpr ->LRESULT FASTCALL {

       if (nCode != HC_ACTION)
           return CallNextHookEx(global::kbrdHook, nCode, wParam, lParam);

       //   std::cout << global::kbrdStruct.vkCode << " " << wParam << std::endl;
         if (wParam == WM_KEYUP || wParam == WM_KEYDOWN) {
             global::kbrdStruct = (*(KBDLLHOOKSTRUCT*)lParam);
             PostMessage(NULL,(UINT)wParam,wParam,global::kbrdStruct.vkCode);
         }
         return CallNextHookEx(global::kbrdHook, nCode, wParam, lParam);
     } };
const auto lowLevelMouseProc{ [](int nCode,WPARAM wParam,LPARAM lParam)constexpr->LRESULT FASTCALL {
    if (nCode != HC_ACTION)
        return CallNextHookEx(global::mouseHook,nCode,wParam,lParam);

        if (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP) {
            PostMessage(NULL,wParam,wParam,wParam);
        }

    return CallNextHookEx(global::mouseHook,nCode,wParam,lParam);
} };

int main() {

    // LPCSTR game = "[#] Grand Theft Auto V [#]";
    // LPCSTR game{ "[#] F.E.A.R. 3 [#]" };
    const HWND gamewindow{ findGameWindow(constants::windowName) };
    const auto winProcId{ gamewindow ? getWindowProcessId(gamewindow) : 0x0 };
    const auto phandle{ winProcId ? openWindowProcessId(winProcId) : 0x0 };

    // Clean side effects
    const auto CLEAN_EXIT{ [&phandle](int exitCode) constexpr -> int {
        CloseHandle(phandle);
        UnhookWindowsHookEx(global::kbrdHook);
        UnhookWindowsHookEx(global::mouseHook);
        return exitCode;
    } };

    if (!winProcId || !gamewindow || !phandle) {
        std::cout << constants::windowName << " Game not found \n";
        return CLEAN_EXIT(EXIT_FAILURE);
    }
    const auto modEntry{ getModuleEntry(winProcId, constants::moduleName) };
    const auto procEntry{ getModuleEntry(winProcId,constants::procName) };

    std::cout << "Game: " << constants::windowName <<
        "\nWindow HWND: " << gamewindow <<
        "\nProcessId: " << winProcId <<
        "\nHandle: " << phandle <<
        "\nmodBaseAddress: " << std::hex << (DWORD_PTR)modEntry.modBaseAddr <<
        "\n---Game is Running---\n";



    // const auto enemeyHoverAdress{ readMemory(phandle, baseAddress + 0x0147E1C0, { 0x23C, 0x138, 0x74, 0x74, 0x20 }, ReturnCode::ADDRESS) }; //fear
    const auto enemeyHoverAdress{ readMemory(phandle, (DWORD_PTR)procEntry.modBaseAddr + 0x84A3E0, { }, ReturnCode::ADDRESS) }; //Alien Swarm: Reactive Drop
    const auto ammoAddress{ readMemory(phandle,(DWORD_PTR)procEntry.modBaseAddr + 0x00823970,{0x20,0x840},ReturnCode::ADDRESS) };
    // auto enemeyHoverAdress = readMemory(phandle, modBaseAddress + 0x2FA5D4, { }, ReturnCode::ADDRESS); //cod4
    // auto enemeyHoverAdress = readMemory(phandle, modBaseAddress + 0x1F46790, { }, ReturnCode::ADDRESS); // gta5

    std::cout << std::dec << "Check Interval: " << constants::checkInterval <<
        "\nMouseInterval: " << constants::mouseDelay <<
        std::endl;

    INPUT mouse[2] = { 0 };
    const auto getEnemeyHover{ [phandle, enemeyHoverAdress, &mouse]() constexpr ->void {
        auto result = readMemory(phandle, (DWORD_PTR)enemeyHoverAdress , { }, ReturnCode::VALUE);
        // auto value = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(result));
        // std::cout << "value" << result << std::endl;
        if (reinterpret_cast<uint_fast64_t>(result) != 0xFFFFFFFF) {
            // std::cout << "hit " << std::dec << result << std::endl;
            sendClick(mouse, 1, constants::mouseDelay);
        }
    } };

    // writeMemory(phandle, value, 0x42CA0000); has issue with data type conversion dicimal to float
    INPUT keyboard[2] = { 0 };
    const auto getAmmo{ [phandle,ammoAddress,&keyboard]() constexpr->void {
        auto result = readMemory(phandle,(DWORD_PTR)ammoAddress,{},ReturnCode::VALUE);
        if (reinterpret_cast<uint_fast64_t>(result) == 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            std::cout << "should press" << std::endl;
            sendKey((WORD)Keys::R,keyboard,1,constants::keyboardDelay);
        }
    } };

    setInterval(getEnemeyHover, constants::checkInterval, []()constexpr->bool { return flag::triggerActive;});
    // setInterval(getAmmo, constants::checkInterval, []()constexpr->bool { return flag::triggerActive;});

    std::cout << "Trigger bot is activated (aim to enemies to auto shoot)\n";

    std::cout << "PGUP to Close\n";

    if (!(global::kbrdHook = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardProc, (HINSTANCE)GetModuleHandle(NULL), 0))) {
        std::cout << "Failed to install keybord Hook! \n";
        return CLEAN_EXIT(EXIT_FAILURE);
    }

    if (!(global::mouseHook = SetWindowsHookEx(WH_MOUSE_LL, lowLevelMouseProc, (HINSTANCE)GetModuleHandle(NULL), 0))) {
        std::cout << "Failed to install mouse Hook! \n";
        return CLEAN_EXIT(EXIT_FAILURE);
    }

    while (
        (GetMessage(&global::msg, NULL, 0, 0) > 0) &&
        static_cast<int>(global::msg.lParam) != static_cast<int>(Keys::PGUP) // exit program on page up
        ) {
        captureKeyPress(global::msg.wParam, global::msg.lParam);
        captureMousePress(global::msg.wParam);
    }

    std::cout << "Closed\n";
    return CLEAN_EXIT(EXIT_SUCCESS);
}