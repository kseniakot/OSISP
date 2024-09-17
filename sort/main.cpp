#include <windows.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>

// Для получения данных о загруженности системы
ULONGLONG fileTimeToULONGLONG(const FILETIME &ft) {
    return (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

double getCpuUsage() {
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        ULONGLONG idle = fileTimeToULONGLONG(idleTime);
        ULONGLONG kernel = fileTimeToULONGLONG(kernelTime);
        ULONGLONG user = fileTimeToULONGLONG(userTime);
        return 100.0 - (idle * 100.0 / (kernel + user));
    }
    return 0.0;
}

struct ThreadData {
    int* array;
    int left;
    int right;
    bool completed = false; // Флаг завершения сортировки
};


// Функция, которую будут выполнять потоки
DWORD WINAPI threadSort(LPVOID param) {
    ThreadData* data = (ThreadData*)param;
    std::sort(data->array + data->left, data->array + data->right + 1);



    data->completed = true;  // Установка флага завершения
    return 0;
}




DWORD WINAPI monitorProgress(LPVOID param) {
    std::vector<ThreadData>* threadData = (std::vector<ThreadData>*)param;

    while (true) {
        int completed = 0;
        std::cout << "\rПрогресс: ";
        for (size_t i = 0; i < threadData->size(); ++i) {
            if ((*threadData)[i].completed) {
                std::cout << "[готов] ";
                completed++;
            } else {
                std::cout << "[не готов] ";
            }
        }

        double cpuUsage = getCpuUsage();
        std::cout << "Загрузка ЦП: " << cpuUsage << "%" << std::flush;

        if (completed == threadData->size()) break;

        Sleep(500);  // Обновление каждые 0.5 секунды
    }

    return 0;
}


void merge(int* arr, int left, int mid, int right) {
    int leftSize = mid - left + 1;
    int rightSize = right - mid;

    std::vector<int> leftArray(leftSize), rightArray(rightSize);
    for (int i = 0; i < leftSize; ++i) {
        leftArray[i] = arr[left + i];
    }
    for (int i = 0; i < rightSize; ++i) {
        rightArray[i] = arr[mid + 1 + i];
    }

    int i = 0, j = 0, k = left;
    while (i < leftSize && j < rightSize) {
        if (leftArray[i] <= rightArray[j]) {
            arr[k++] = leftArray[i++];
        }
        else {
            arr[k++] = rightArray[j++];
        }
    }
    while (i < leftSize) {
        arr[k++] = leftArray[i++];
    }
    while (j < rightSize) {
        arr[k++] = rightArray[j++];
    }
}

// Функция, которую будут выполнять потоки для слияния
DWORD WINAPI threadMerge(LPVOID param) {
    ThreadData* data = (ThreadData*)param;
    int mid = data->left + (data->right - data->left) / 2;
    merge(data->array, data->left, mid, data->right);
    return 0;
}

void parallelSort(int* arr, int size, int numThreads) {

    std::vector<HANDLE> threads(numThreads);
    std::vector<ThreadData> threadData(numThreads);

    int chunkSize = size / numThreads;

    // Запуск потоков для сортировки своих частей массива
    for (int i = 0; i < numThreads; ++i) {
        int left = i * chunkSize;
        int right = (i == numThreads - 1) ? size - 1 : (left + chunkSize - 1);

        threadData[i] = { arr, left, right };
        threads[i] = CreateThread(NULL, 0, threadSort, &threadData[i], 0, NULL);
    }

    // Запуск потока для мониторинга прогресса
    HANDLE monitorThread = CreateThread(NULL, 0, monitorProgress, &threadData, 0, NULL);

    // Ожидание завершения всех потоков сортировки
    WaitForMultipleObjects(numThreads, threads.data(), TRUE, INFINITE);

    for (int sizeMerge = chunkSize; sizeMerge < size; sizeMerge *= 2) {
        int numMerges = (size + 2 * sizeMerge - 1) / (2 * sizeMerge);

        for (int i = 0; i < numMerges; ++i) {
            int left = i * 2 * sizeMerge;
            int right = std::min(left + 2 * sizeMerge - 1, size - 1);

            threadData[i] = { arr, left, right, false};
            threads[i] = CreateThread(NULL, 0, threadMerge, &threadData[i], 0, NULL);
        }
        // Ожидание завершения всех потоков слияния
        WaitForMultipleObjects(numMerges, threads.data(), TRUE, INFINITE);
    }

    // Закрытие дескрипторов потоков
    for (HANDLE& thread : threads) {
        CloseHandle(thread);
    }
}


int main() {
    int size = 1'000'000'000, numThreads =64;

    std::cout << "Введите размер массива: " << size << "\n";

    std::cout << "Введите количество потоков: " << numThreads << "\n";

    if (numThreads < 1 || size < numThreads) {
        std::cout << "Некорректное количество потоков или размер массива.\n";
        return -1;
    }
    std::vector<int> array(size);
    for (int j = 0; j < size; j++) {
        array[j] = j % 10;
    }

    double initialCpuUsage = getCpuUsage();
    std::cout << "Загрузка ЦП перед началом: " << initialCpuUsage << "%\n";

    // Замер времени
    auto start = std::chrono::high_resolution_clock::now();

    parallelSort(array.data(), size, numThreads);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout << "\nВремя выполнения: " << duration.count() << " секунд\n";

    double finalCpuUsage = getCpuUsage();
    std::cout << "Загрузка ЦП после выполнения: " << finalCpuUsage << "%\n";
    std::cout << "Изменение загрузки ЦП: " << finalCpuUsage - initialCpuUsage << "%\n";

    for (size_t i = 0; i < size; ++i) {
        std::cout << array[i];
    }
    return 0;
}