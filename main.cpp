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

enum class ReturnCode {
    ADDRESS = 0x1,
    VALUE = 0x0
};

auto findGameWindow{ [](LPCSTR windowName) constexpr {
    return FindWindowEx(NULL, 0, 0, windowName);
} };

auto getWindowProcessId{ [](HWND gameWindow)  constexpr {
    DWORD processId { 0x0 };
    if (!gameWindow) {
        std::cout << "Game window Not found\n";
        return processId;
    }
    GetWindowThreadProcessId(gameWindow, &processId);
    return processId;
} };

auto openWindowProcessId{ [](DWORD processId) constexpr {
    if (!processId) {
        std::cout << "Failed to get process id" << std::endl;
        return (HANDLE)0;
    }
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
} };

auto getProcessBaseAddress{ [](DWORD proc, const char* modName) constexpr {
    uintptr_t modBaseAddr { 0x0 };
    HANDLE hSnap { CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, proc) };

    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 modEntry  {};
        modEntry.dwSize = sizeof(modEntry);
        if (Module32First(hSnap, &modEntry)) {
            do {
                if (strcmp((const char*)modEntry.szModule, modName)) {
                    modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
} };

auto writeMemory{ [](HANDLE window, LPVOID address, DWORD value) constexpr {
    // WriteProcessMemory(window, (BYTE *)address, &value, sizeof(&value), NULL);
    WriteProcessMemory(window, address, &value, sizeof(&value), NULL);
} };
auto readMemory{ [](HANDLE phandle, DWORD_PTR baseAddress, std::vector<DWORD> offsets, ReturnCode code) constexpr {
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

auto setInterval{ [](std::function<void(void)> func, uint_fast32_t interval, std::function<bool(void)> condition) constexpr {
    std::thread([func, interval, condition]() {
        do {
            if (condition()) {
                func();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        } while (true);
    }).detach();
} };

auto sendKey{ [](WORD key, INPUT kyBrd[2], unsigned int repeat) constexpr {
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
auto sendClick{ [](INPUT mouse[2], int repeat, DWORD delay) constexpr {
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

int main() {
    // LPCSTR game = "[#] Grand Theft Auto V [#]";
    LPCSTR game{ "[#] F.E.A.R. 3 [#]" };
    HWND gamewindow{ findGameWindow(game) };
    auto processId{ gamewindow ? getWindowProcessId(gamewindow) : 0x0 };
    auto phandle{ processId ? openWindowProcessId(processId) : 0x0 };

    if (!processId || !gamewindow || !phandle) {
        std::cout << game << " Game not found \n";
        return EXIT_FAILURE;
    }

    auto baseAddress{ getProcessBaseAddress(processId, (const char*)game) };
    std::cout << "Game: " << game <<
        "\nWindow HWND: " << gamewindow <<
        "\nProcessId: " << processId <<
        "\nHandle: " << phandle <<
        "\nBaseAddress: " << std::hex << (DWORD)baseAddress <<
        "\n---Game is Running---\n";



    auto enemeyHoverAdress{ readMemory(phandle, baseAddress + 0x0147E1C0, { 0x23C, 0x138, 0x74, 0x74, 0x20 }, ReturnCode::ADDRESS) }; //fear
    // auto enemeyHoverAdress = readMemory(phandle, baseAddress + 0x2FA5D4, { }, ReturnCode::ADDRESS); //cod4
    // auto enemeyHoverAdress = readMemory(phandle, baseAddress + 0x1F46790, { }, ReturnCode::ADDRESS); // gta5
    constexpr uint_fast32_t checkInterval{ 80 };
    constexpr DWORD mouseDelay{ 90 };
    std::cout << std::dec << "Check Interval: " << checkInterval <<
        "\nMouseInterval: " << mouseDelay <<
        std::endl;

    INPUT mouse[2];
    auto getEnemeyHover{ [phandle, enemeyHoverAdress, &mouse, mouseDelay]() {
        auto result = readMemory(phandle, (DWORD_PTR)enemeyHoverAdress , { }, ReturnCode::VALUE); //fear
        // auto value = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(result));
        // std::cout << "value" << result << std::endl;
        if (result) {
            // std::cout << "hit " << value << std::endl;
            sendClick(mouse, 1, mouseDelay);
        }
    } };

    // writeMemory(phandle, value, 0x42CA0000); has issue with data type conversion dicimal to float

    setInterval(getEnemeyHover, checkInterval, []() constexpr {
        return (bool)GetAsyncKeyState(VK_RBUTTON);
    });

    std::cout << "Trigger bot is activated (aim to enemies to auto shoot)\n";

    std::cout << "hold PGUP to Close\n";
    while (!GetAsyncKeyState(VK_PRIOR) && findGameWindow(game)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    std::cout << "Closed\n";
    CloseHandle(phandle);
    return EXIT_SUCCESS;
}