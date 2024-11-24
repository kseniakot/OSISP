#include <windows.h>
#include <iostream>

const int BUF_SIZE = 256;

int main() {
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hMapFile = OpenFileMappingW(
            FILE_MAP_ALL_ACCESS,
            FALSE,
            L"Local\\MySharedMemory1"
    );

    if (hMapFile == NULL) {
        std::cerr << "Не удалось открыть разделяемую память: " << GetLastError() << std::endl;
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

    HANDLE hMutex = OpenMutexW(
            MUTEX_ALL_ACCESS,
            FALSE,
            L"Local\\Mutex12"
    );

    if (hMutex == NULL) {
        std::cerr << "Не удалось открыть мьютекс: " << GetLastError() << std::endl;
        UnmapViewOfFile(pBuf);
        CloseHandle(hMapFile);
        return 1;
    }

    while (true) {
        WaitForSingleObject(hMutex, INFINITE);
        std::cout << "Прочитано из разделяемой памяти:" << std::endl;
        if (strcmp((const char*)pBuf, "exit") == 0) {
            std::cout << "Получен сигнал на завершение. Закрытие Reader." << std::endl;
            ReleaseMutex(hMutex);
            break;
        }
        std::cout << pBuf << std::endl;

        ReleaseMutex(hMutex);
        Sleep(100);
    }

    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    CloseHandle(hMutex);

    return 0;
}