#include <windows.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <mutex>

std::mutex coutMutex;

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
    int threadId;  // Идентификатор потока
};


// Функция для сортировки массива (quicksort)
//void quickSort(int* arr, int left, int right) {
//    if (left < right) {
//        int pivot = arr[right];
//        int i = left - 1;
//        for (int j = left; j < right; ++j) {
//            if (arr[j] <= pivot) {
//                std::swap(arr[++i], arr[j]);
//            }
//        }
//        std::swap(arr[i + 1], arr[right]);
//        quickSort(arr, left, i);
//        quickSort(arr, i + 2, right);
//    }
//}

// Функция, которую будут выполнять потоки
DWORD WINAPI threadSort(LPVOID param) {
    ThreadData* data = (ThreadData*)param;

    // Получение загрузки ЦП перед началом работы потока
    double initialCpuUsage = getCpuUsage();

    std::sort(data->array + data->left, data->array + data->right + 1);

    {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "Поток " << data->threadId << ": готов\n";
        double finalCpuUsage = getCpuUsage();
        std::cout << "Поток " << data->threadId << ": Загрузка ЦП до: " << initialCpuUsage << "%, после: " << finalCpuUsage << "%\n";
    }

    data->completed = true;  // Установка флага завершения
    return 0;
}

// Функция для слияния отсортированных частей
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

// Функция для параллельной сортировки
void parallelSort(int* arr, int size, int numThreads) {
    std::vector<HANDLE> threads(numThreads);
    std::vector<ThreadData> threadData(numThreads);

    int chunkSize = size / numThreads;

    // Запуск потоков для сортировки своих частей массива
    for (int i = 0; i < numThreads; ++i) {
        int left = i * chunkSize;
        int right = (i == numThreads - 1) ? size - 1 : (left + chunkSize - 1);

        threadData[i] = { arr, left, right, 0 };  // Идентификатор потока будет установлен ниже
        threadData[i].threadId = i;  // Установка идентификатора потока
        threads[i] = CreateThread(NULL, 0, threadSort, &threadData[i], 0, NULL);
    }

    // Ожидание завершения всех потоков сортировки
    WaitForMultipleObjects(numThreads, threads.data(), TRUE, INFINITE);

    // Закрытие дескрипторов потоков сортировки
    for (HANDLE& thread : threads) {
        CloseHandle(thread);
    }

    // Слияние отсортированных частей массива
    for (int sizeMerge = chunkSize; sizeMerge < size; sizeMerge *= 2) {
        for (int i = 0; i < size; i += 2 * sizeMerge) {
            int mid = std::min(i + sizeMerge - 1, size - 1);
            int right = std::min(i + 2 * sizeMerge - 1, size - 1);
            merge(arr, i, mid, right);
        }
    }
}


int main() {
    int size = 1'000'000, numThreads = 1;

    std::cout << "Введите размер массива: ";
    std::cout << size << "\n";  // Ввод массива пропущен для упрощения

    std::cout << "Введите количество потоков: ";
    std::cout << numThreads << "\n";  // Ввод количества потоков пропущен для упрощения

    if (numThreads < 1 || size < numThreads) {
        std::cout << "Некорректное количество потоков или размер массива.\n";
        return -1;
    }

    // Инициализация массива
    std::vector<int> array(size);
    for (int j = 0; j < size; j++) {
        array[j] = j % 10;  // Заполнение массива для тестирования
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

    for (size_t i = 0; i < size; ++i) {
        std::cout << array[i];
    }

    return 0;
}
