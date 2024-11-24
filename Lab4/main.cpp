#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

struct Record {
    int id;
    char name[50];
};

std::vector<bool> dataReady;
SRWLOCK srwLock;
HANDLE hCoutMutex;

struct WriteParams {
    LPVOID mappedView;
    int index;
    int id;
    const char* name;
};

struct ReadParams {
    LPVOID mappedView;
    int index;
};

HANDLE CreateAndMapFile(LPCSTR filename, DWORD size, HANDLE &hFile, LPVOID &mappedView) {
    hFile = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create file" << std::endl;
        return NULL;
    }

    if (SetFilePointer(hFile, size, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        std::cerr << "Failed to set file pointer with error: " << GetLastError() << std::endl;
        CloseHandle(hFile);
        return NULL;
    }
    if (!SetEndOfFile(hFile)) {
        std::cerr << "Failed to set end of file with error: " << GetLastError() << std::endl;
        CloseHandle(hFile);
        return NULL;
    }

    HANDLE hMapFile = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, size, NULL);
    if (!hMapFile) {
        std::cerr << "Failed to create file mapping" << std::endl;
        CloseHandle(hFile);
        return NULL;
    }

    mappedView = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!mappedView) {
        std::cerr << "Failed to map file view" << std::endl;
        CloseHandle(hMapFile);
        CloseHandle(hFile);
        return NULL;
    }

    return hMapFile;
}

DWORD WINAPI WriteRecord(LPVOID params) {
    auto* wp = static_cast<WriteParams*>(params);

    AcquireSRWLockExclusive(&srwLock);

    Record* records = static_cast<Record*>(wp->mappedView);
    records[wp->index].id = wp->id;
    strncpy(records[wp->index].name, wp->name, sizeof(records[wp->index].name) - 1);
    records[wp->index].name[sizeof(records[wp->index].name) - 1] = '\0';

    dataReady[wp->index] = true;

    ReleaseSRWLockExclusive(&srwLock);

    WaitForSingleObject(hCoutMutex, INFINITE);
    std::cout << "Writer Thread - Written Record #" << wp->index + 1 << ": ID = " << wp->id << ", Name = " << wp->name << std::endl;
    ReleaseMutex(hCoutMutex);

    delete wp;
    return 0;
}

DWORD WINAPI ReadRecord(LPVOID params) {
    auto* rp = static_cast<ReadParams*>(params);

    if (dataReady[rp->index]) {
        AcquireSRWLockShared(&srwLock);

        Record* records = static_cast<Record*>(rp->mappedView);

        WaitForSingleObject(hCoutMutex, INFINITE);
        std::cout << "Reader Thread - Read Record #" << rp->index + 1 << ": ID = " << records[rp->index].id << ", Name = " << records[rp->index].name << std::endl;
        ReleaseMutex(hCoutMutex);

        ReleaseSRWLockShared(&srwLock);
    } else {
        WaitForSingleObject(hCoutMutex, INFINITE);
        std::cout << "Reader Thread - Record #" << rp->index + 1 << " is not yet written, skipping." << std::endl;
        ReleaseMutex(hCoutMutex);
    }

    delete rp;
    return 0;
}

int main() {
    LPVOID mappedView;
    HANDLE hFile;
    int maxRecords = 10;
    const DWORD initialSize = sizeof(Record) * maxRecords;

    InitializeSRWLock(&srwLock);
    hCoutMutex = CreateMutex(NULL, FALSE, NULL);

    HANDLE hMapFile = CreateAndMapFile("database2.bin", initialSize, hFile, mappedView);

    if (hMapFile) {
        dataReady.resize(maxRecords, false);

        HANDLE threads[10];

        for (int i = 0; i < 5; i++) {
            auto* params = new WriteParams{mappedView, i, i, "Writer"};
            threads[i] = CreateThread(NULL, 0, WriteRecord, params, 0, NULL);
        }

        for (int i = 0; i < 5; i++) {
            auto* params = new ReadParams{mappedView, i};
            threads[i + 5] = CreateThread(NULL, 0, ReadRecord, params, 0, NULL);
        }

        WaitForMultipleObjects(10, threads, TRUE, INFINITE);

        for (auto& thread : threads) {
            CloseHandle(thread);
        }

        UnmapViewOfFile(mappedView);
        CloseHandle(hMapFile);
        CloseHandle(hFile);
    }

    CloseHandle(hCoutMutex);

    return 0;
}
