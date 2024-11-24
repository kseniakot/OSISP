#include <windows.h>
#include <iostream>
#include <string>

struct Record {
    int id;
    char name[50];
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

void WriteRecord(LPVOID mappedView, int index, int id, const char* name) {
    Record* records = static_cast<Record*>(mappedView);
    records[index].id = id;
    strncpy(records[index].name, name, sizeof(records[index].name) - 1);
    records[index].name[sizeof(records[index].name) - 1] = '\0';
}

void DeleteRecord(LPVOID mappedView, int index, int &recordCount) {
    if (recordCount <= 0 || index >= recordCount) {
        std::cerr << "Invalid index or no records to delete." << std::endl;
        return;
    }

    Record* records = static_cast<Record*>(mappedView);

    if (index != recordCount - 1) {
        records[index] = records[recordCount - 1];
    }

    records[recordCount - 1].id = -1;
    strcpy(records[recordCount - 1].name, "");

    recordCount--;
}


HANDLE ResizeFileMapping(HANDLE hFile, HANDLE hMapFile, LPVOID &mappedView, DWORD newSize) {

    if (!UnmapViewOfFile(mappedView)) {
        std::cerr << "Failed to unmap view of file with error: " << GetLastError() << std::endl;
        return NULL;
    }
    CloseHandle(hMapFile);

    if (SetFilePointer(hFile, newSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        std::cerr << "Failed to set file pointer with error: " << GetLastError() << std::endl;
        return NULL;
    }

    if (!SetEndOfFile(hFile)) {
        std::cerr << "Failed to set end of file with error: " << GetLastError() << std::endl;
        return NULL;
    }


    hMapFile = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, newSize, NULL);
    if (hMapFile == NULL) {
        std::cerr << "CreateFileMapping failed with error: " << GetLastError() << std::endl;
        return NULL;
    }


    mappedView = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, newSize);
    if (!mappedView) {
        std::cerr << "Failed to map view of file with error: " << GetLastError() << std::endl;
        CloseHandle(hMapFile);
        return NULL;
    }

    return hMapFile;
}


void ManageFileSize(HANDLE hFile, HANDLE &hMapFile, LPVOID &mappedView, int &recordCount, int &maxRecords) {
    const int increaseStep = static_cast<int>(maxRecords / 2);
    DWORD newSize;

    if (recordCount >= maxRecords) {
        newSize = sizeof(Record) * (maxRecords + increaseStep);
        hMapFile = ResizeFileMapping(hFile, hMapFile, mappedView, newSize);
        if (hMapFile) {
            maxRecords += increaseStep;
            std::cout << "File resized. New limit: " << maxRecords << " records." << std::endl;
        } else {
            std::cerr << "Failed to resize the file!" << std::endl;
            return;
        }
    }


    if (recordCount < maxRecords / 2 && maxRecords > increaseStep) {
        newSize = sizeof(Record) * (maxRecords - increaseStep);
        hMapFile = ResizeFileMapping(hFile, hMapFile, mappedView, newSize);
        if (hMapFile) {
            maxRecords -= increaseStep;
            std::cout << "File shrunk. New limit: " << maxRecords << " records." << std::endl;
        } else {
            std::cerr << "Failed to shrink the file!" << std::endl;
            return;
        }
    }
}

int main() {
    LPVOID mappedView;
    HANDLE hFile;
    int maxRecords = 10;
    const DWORD initialSize = sizeof(Record) * maxRecords;

    HANDLE hMapFile = CreateAndMapFile("database2.bin", initialSize, hFile, mappedView);

    if (hMapFile) {
        int recordCount = 0;

        for(int i = 0; i < 20; i++) {
            WriteRecord(mappedView, recordCount++, i, "Object");
            std::cout << "Record added. Total records: " << recordCount << std::endl;
            ManageFileSize(hFile, hMapFile, mappedView, recordCount, maxRecords);
        }

        std::cout << "Before deletion:" << std::endl;
        Record* records = static_cast<Record*>(mappedView);
        for (int i = 0; i < recordCount; i++) {
            std::cout << "ID: " << records[i].id << ", Name: " << records[i].name << std::endl;
        }

        for(int i = 0; i < recordCount; i++) {
            DeleteRecord(mappedView, i, recordCount);
            std::cout << "Record deleted. Total records: " << recordCount << std::endl;
            ManageFileSize(hFile, hMapFile, mappedView, recordCount, maxRecords);
        }

        std::cout << "After deletion:" << std::endl;
        for (int i = 0; i < recordCount; i++) {
            std::cout << "ID: " << records[i].id << ", Name: " << records[i].name << std::endl;
        }

        UnmapViewOfFile(mappedView);
        CloseHandle(hMapFile);
        CloseHandle(hFile);
    }

    return 0;
}
