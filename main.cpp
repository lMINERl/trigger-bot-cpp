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

#define RET_ADDRESS true
#define RET_VALUE false

auto findGameWindow = [](LPCSTR windowName = NULL) {
    return FindWindowEx(NULL, 0, 0, windowName);
};
auto getWindowProcessId = [](HWND gameWindow) {
    DWORD processId;
    if (!gameWindow) {
        std::cout << "Game window Not found" << std::endl;
        return processId;
    }
    GetWindowThreadProcessId(gameWindow, &processId);
    return processId;
};
auto openWindowProcessId = [](DWORD processId) {
    if (!processId) {
        std::cout << "Failed to get process id" << std::endl;
        return (HANDLE)0;
    }
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
};

auto getProcessBaseAddress = [](DWORD proc, const char* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, proc);

    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 modEntry;
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
};

auto writeMemory = [](HANDLE window, LPVOID address, DWORD value) {
    // WriteProcessMemory(window, (BYTE *)address, &value, sizeof(&value), NULL);
    WriteProcessMemory(window, address, &value, sizeof(&value), NULL);
};
auto readMemory = [](HANDLE phandle, DWORD_PTR baseAddress, std::vector<DWORD> offsets, bool getAddress = false) {
    LPVOID address_PTR = (LPVOID)baseAddress;
    DWORD temp = 0x0;
    ReadProcessMemory(phandle, address_PTR, &temp, sizeof(temp), NULL);
    // std::cout << std::hex << address_PTR << "-" << temp << std::endl;
    for (unsigned int i = 0; i < offsets.size(); ++i) {
        address_PTR = (LPVOID)(temp + offsets[i]);
        ReadProcessMemory(phandle, address_PTR, &temp, sizeof(temp), NULL);
        // std::cout << std::hex << address_PTR << "-" << temp << std::endl;
    }
    return getAddress ? address_PTR : (LPVOID)temp;
};

auto setInterval = [](std::function<void(void)> func, uint_fast32_t interval, std::function<bool(void)> condition) {
    std::thread([func, interval, condition]() {
        WINBOOL s = FALSE;
        do {
            s = condition();
            if (s) {
                func();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        } while (true);
        }).detach();
};

auto sendKey = [](WORD key, INPUT kyBrd[2], unsigned int repeat) {
    while (repeat > 0) {
        kyBrd[0].type = kyBrd[1].type = INPUT_KEYBOARD;
        kyBrd[0].ki.wVk = kyBrd[1].ki.wVk = 0;
        kyBrd[0].ki.dwFlags = KEYEVENTF_SCANCODE;
        kyBrd[0].ki.wScan = kyBrd[1].ki.wScan = key;
        SendInput(1, &kyBrd[0], sizeof(INPUT));
        kyBrd[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SendInput(1, &kyBrd[1], sizeof(INPUT));
        --repeat;
    }
};
auto sendClick = [](INPUT mouse[2], int repeat, DWORD delay) {
    mouse[0].type = mouse[1].type = INPUT_MOUSE;
    mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    do {
        SendInput(1, &mouse[0], sizeof(INPUT));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        SendInput(1, &mouse[1], sizeof(INPUT));
        --repeat;
    } while (repeat > 0);
};

int main() {
    LPCSTR game = "[#] F.E.A.R. 3 [#]";
    HWND gamewindow = findGameWindow(game);
    DWORD processId = gamewindow ? getWindowProcessId(gamewindow) : 0x0;
    HANDLE phandle = processId ? openWindowProcessId(processId) : 0x0;

    if (!processId || !gamewindow || !phandle) {
        return 0;
    }

    auto baseAddress = getProcessBaseAddress(processId, (const char*)game);
    std::cout << "Game: " << game <<
        "\nWindow HWND: " << gamewindow <<
        "\nProcessId: " << processId <<
        "\nHandle: " << phandle <<
        "\nBaseAddress: " << std::hex << (DWORD)baseAddress <<
        "\n---Game is Running---" << std::endl;


    uint_fast32_t checkInterval = 79;
    DWORD mouseInterval = 100;
    std::cout << std::dec << "Check Interval: " << checkInterval <<
        "\nMouseInterval: " << mouseInterval <<
        std::endl;

    INPUT mouse[2];
    auto getEnemeyHover = [phandle, baseAddress, &mouse, mouseInterval]() {
        auto value = readMemory(phandle, baseAddress + 0x0147E1C0, { 0x23C, 0x138, 0x74, 0x74, 0x20 }, RET_VALUE);
        // std::cout << "value" << value << std::endl;
        if (value) {
            // std::cout << "hit" << std::endl;
            sendClick(mouse, 1, mouseInterval);
        }
    };

    // writeMemory(phandle, value, 0x42CA0000); has issue with data type conversion dicimal to float

    setInterval(getEnemeyHover, checkInterval, []() { return (WINBOOL)GetAsyncKeyState(VK_RBUTTON); });
    std::cout << "Trigger bot is activated (aim to enemies to auto shoot)" << std::endl;

    std::cout << "PGUP to Close" << std::endl;
    while (!GetAsyncKeyState(VK_PRIOR)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Closed";
    CloseHandle(phandle);
    return 0;
}