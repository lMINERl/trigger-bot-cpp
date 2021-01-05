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
    capslook = 1,
    PGUP = VK_PRIOR
};
enum class ReturnCode {
    ADDRESS = 0x1,
    VALUE = 0x0
};

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
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        } while (true);
    }).detach();
} };

const auto sendKey{ [](WORD key, INPUT kyBrd[2], unsigned int repeat) constexpr->void {
    kyBrd[0].type = kyBrd[1].type = INPUT_KEYBOARD;
    kyBrd[0].ki.wVk = kyBrd[1].ki.wVk = 0;
    kyBrd[0].ki.dwFlags = KEYEVENTF_SCANCODE;
    kyBrd[0].ki.wScan = kyBrd[1].ki.wScan = key;
    kyBrd[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    do {
        SendInput(1, &kyBrd[0], sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SendInput(1, &kyBrd[1], sizeof(INPUT));
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

KBDLLHOOKSTRUCT kbstrct{};
HHOOK kbrdHook{};
auto lowLevelKeyboardProc{ [](int nCode, WPARAM wParam, LPARAM lParam) constexpr ->LRESULT WINAPI {

       if (nCode != HC_ACTION)
           return CallNextHookEx(kbrdHook, nCode, wParam, lParam);

       std::cout << kbstrct.vkCode << std::endl;
       if (wParam == WM_KEYDOWN) {
           kbstrct = (*(KBDLLHOOKSTRUCT*)lParam);
           // a key (non-system) is pressed.
           switch (kbstrct.vkCode) {

           }
           if (kbstrct.vkCode == VK_F1) {
               // F1 is pressed!
               std::cout << kbstrct.vkCode << std::endl;
           }
       }
       // std::cout << __FUNCTION__ << std::endl;
       return CallNextHookEx(kbrdHook, nCode, wParam, lParam);
   } };


namespace constants {
    inline constexpr LPCSTR windowName{ "Alien Swarm: Reactive Drop" };
    inline constexpr LPCSTR moduleName{ "reactivedrop.exe" };
    inline constexpr LPCSTR procName{ "client.dll" };
    inline constexpr uint_fast32_t checkInterval{ 80 };
    inline constexpr DWORD mouseDelay{ 90 };
}
int main() {

    // LPCSTR game = "[#] Grand Theft Auto V [#]";
    // LPCSTR game{ "[#] F.E.A.R. 3 [#]" };
    const HWND gamewindow{ findGameWindow(constants::windowName) };
    const auto winProcId{ gamewindow ? getWindowProcessId(gamewindow) : 0x0 };
    const auto phandle{ winProcId ? openWindowProcessId(winProcId) : 0x0 };

    const auto CLEAN_EXIT{ [&phandle](int exitCode) constexpr -> int {
        CloseHandle(phandle);
        UnhookWindowsHookEx(kbrdHook);
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
    // auto enemeyHoverAdress = readMemory(phandle, modBaseAddress + 0x2FA5D4, { }, ReturnCode::ADDRESS); //cod4
    // auto enemeyHoverAdress = readMemory(phandle, modBaseAddress + 0x1F46790, { }, ReturnCode::ADDRESS); // gta5

    std::cout << std::dec << "Check Interval: " << constants::checkInterval <<
        "\nMouseInterval: " << constants::mouseDelay <<
        std::endl;

    INPUT mouse[2];
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

    bool active{ false };
    setInterval([&active]() constexpr {
        std::cout << "active: " << active << "\n";
        active = !active;
    }, 50, []() constexpr {return GetAsyncKeyState(VK_CAPITAL);});

    setInterval(getEnemeyHover, constants::checkInterval, [&active]() constexpr ->bool {
        // return static_cast<bool>(GetAsyncKeyState(VK_RBUTTON));
        // std::cout << active << " <<=\n";
        return active;
    });


    std::cout << "Trigger bot is activated (aim to enemies to auto shoot)\n";

    std::cout << "hold PGUP to Close\n";






    if (!(kbrdHook = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardProc, (HINSTANCE)GetModuleHandle(NULL), 0))) {
        MessageBox(NULL, "Failed to install hook!", "Error", MB_ICONERROR);
    }
    MSG msg{};
    //  !GetAsyncKeyState(static_cast<int>(Keys::PGUP))
    while (GetMessage(&msg, 0, 0, 0)) {

        std::cout << "im sleep " << msg.lParam;
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }

    std::cout << "Closed\n";
    return CLEAN_EXIT(EXIT_SUCCESS);
}