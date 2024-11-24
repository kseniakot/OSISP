#include <windows.h>
#include <iostream>

const int BUF_SIZE = 256;

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Программа для записи данных в разделяемую память.\n";

    HANDLE hMapFile = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            BUF_SIZE,
            L"Local\\MySharedMemory1"
    );

    if (hMapFile == NULL) {
        std::cerr << "Не удалось создать разделяемую память: " << GetLastError() << std::endl;
        return 1;
    }

    LPCTSTR pBuf = (LPTSTR)MapViewOfFile(
            hMapFile,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            BUF_SIZE
    );

    if (pBuf == NULL) {
        std::cerr << "Не удалось отобразить разделяемую память: " << GetLastError() << std::endl;
        CloseHandle(hMapFile);
        return 1;
    }

    HANDLE hMutex = CreateMutexW(
            NULL,
            FALSE,
            L"Local\\Mutex12"
    );

    if (hMutex == NULL) {
        std::cerr << "Не удалось создать мьютекс: " << GetLastError() << std::endl;
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return 1;
    }

    while (true) {
        std::cout << "Ожидание ввода сообщения...\n";
        WaitForSingleObject(hMutex, INFINITE);

        std::cout << "Введите сообщение для записи в разделяемую память (введите 'exit' для выхода): ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "exit") {
            CopyMemory((PVOID)pBuf, "exit", 5 * sizeof(char));
            break;
        }

        std::string currentData = (char*)pBuf;
        if (!currentData.empty()) {
            currentData += "\n";
        }
        currentData += input;
        CopyMemory((PVOID)pBuf, currentData.c_str(), (currentData.size() + 1) * sizeof(char));
        std::cout << "Данные записаны в разделяемую память.\n";

        ReleaseMutex(hMutex);
    }

    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    CloseHandle(hMutex);

    return 0;
}