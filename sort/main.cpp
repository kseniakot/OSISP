#include <windows.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <numeric>
#include <Pdh.h>


std::mutex coutMutex;

static PDH_HQUERY cpuQuery;
static PDH_HCOUNTER cpuTotal;
bool monitoringCpu = true; // Флаг для завершения потока мониторинга
std::vector<double> cpuUsageValues; // Вектор для хранения значений загрузки CPU

void initCpuQuery() {
    PdhOpenQuery(NULL, 0, &cpuQuery);
    PdhAddEnglishCounterW(cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
    PdhCollectQueryData(cpuQuery);
    Sleep(1000);
}

double getCpuUsage() {
    PDH_FMT_COUNTERVALUE counterVal;
    PdhCollectQueryData(cpuQuery);
    Sleep(1000);
    PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
    return counterVal.doubleValue;
}

DWORD WINAPI monitorCpuUsage(LPVOID param) {
    while (monitoringCpu) {
        double cpuUsage = getCpuUsage();
        {
            std::lock_guard<std::mutex> lock(coutMutex);
            cpuUsageValues.push_back(cpuUsage); // Сохраняем значения в вектор
        }
        Sleep(1000); // Замеры каждые 1000 миллисекунд
    }
    return 0;
}

struct ThreadData {
    int* array;
    int left;
    int right;
    int threadId;
};

DWORD WINAPI threadSort(LPVOID param) {
    ThreadData* data = (ThreadData*)param;
    std::sort(data->array + data->left, data->array + data->right + 1);

    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "\nThread " << data->threadId << ": done\n";
    return 0;
}

struct ThreadMergeData {
    int* array;
    int left;
    int mid;
    int right;
    int threadId;
};

void merge(int* arr, int left, int mid, int right) {
    int leftSize = mid - left + 1;
    int rightSize = right - mid;
    std::vector<int> leftArray(leftSize), rightArray(rightSize);

    for (int i = 0; i < leftSize; ++i) leftArray[i] = arr[left + i];
    for (int i = 0; i < rightSize; ++i) rightArray[i] = arr[mid + 1 + i];

    int i = 0, j = 0, k = left;
    while (i < leftSize && j < rightSize) {
        if (leftArray[i] <= rightArray[j]) arr[k++] = leftArray[i++];
        else arr[k++] = rightArray[j++];
    }
    while (i < leftSize) arr[k++] = leftArray[i++];
    while (j < rightSize) arr[k++] = rightArray[j++];
}

DWORD WINAPI threadMerge(LPVOID param) {
    ThreadMergeData* data = (ThreadMergeData*)param;
    merge(data->array, data->left, data->mid, data->right);

    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "\nThread " << data->threadId << ": merged " << data->left << " to " << data->right << "\n";
    return 0;
}

void parallelMerge(int* arr, int size, int numThreads) {
    int chunkSize = size / numThreads;
    std::vector<HANDLE> threads(numThreads);
    std::vector<ThreadMergeData> threadData(numThreads);

    for (int mergeSize = chunkSize; mergeSize < size; mergeSize *= 2) {
        int currentThreads = numThreads / 2;
        threads.resize(currentThreads);
        threadData.resize(currentThreads);

        for (int i = 0; i < currentThreads; ++i) {
            int left = i * 2 * mergeSize;
            int mid = std::min(left + mergeSize - 1, size - 1);
            int right = std::min(left + 2 * mergeSize - 1, size - 1);

            threadData[i] = { arr, left, mid, right, i };
            threads[i] = CreateThread(NULL, 0, threadMerge, &threadData[i], 0, NULL);
        }

        WaitForMultipleObjects(currentThreads, threads.data(), TRUE, INFINITE);
        for (HANDLE& thread : threads) CloseHandle(thread);

        numThreads /= 2;
    }
}

void parallelSort(int* arr, int size, int numThreads) {
    std::vector<HANDLE> threads(numThreads);
    std::vector<ThreadData> threadData(numThreads);

    int chunkSize = size / numThreads;

    // Фаза сортировки: каждый поток сортирует свою часть
    for (int i = 0; i < numThreads; ++i) {
        int left = i * chunkSize;
        int right = (i == numThreads - 1) ? size - 1 : (left + chunkSize - 1);
        threadData[i] = { arr, left, right, i };
        threads[i] = CreateThread(NULL, 0, threadSort, &threadData[i], 0, NULL);
    }

    WaitForMultipleObjects(numThreads, threads.data(), TRUE, INFINITE);
    for (HANDLE& thread : threads) CloseHandle(thread);

    // Фаза слияния: многопоточное слияние отсортированных частей
    parallelMerge(arr, size, numThreads);
}

int main() {
    int size = 1'000'000'000, numThreads = 1;
    std::cout << "Array size: " << size << "\n";
    std::cout << "Number of threads: " << numThreads << "\n";

    if (numThreads < 1 || size < numThreads) {
        std::cout << "Invalid number of threads or array size.\n";
        return -1;
    }

    std::vector<int> array(size);
    for (int j = 0; j < size; j++) array[j] = j % 10;

    initCpuQuery();

    double initialCpuUsage = getCpuUsage();
    std::cout << "CPU load before start: " << initialCpuUsage << "%\n";

    // Запуск мониторинга CPU в отдельном потоке
    HANDLE cpuMonitorThread = CreateThread(NULL, 0, monitorCpuUsage, NULL, 0, NULL);

    auto start = std::chrono::high_resolution_clock::now();
    parallelSort(array.data(), size, numThreads);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> duration = end - start;
    std::cout << "\nExecution time: " << duration.count() << " seconds\n";

    // Остановка потока мониторинга CPU
    monitoringCpu = false;
    WaitForSingleObject(cpuMonitorThread, INFINITE);
    CloseHandle(cpuMonitorThread);

    double finalCpuUsage = getCpuUsage();
    std::cout << "CPU load after execution: " << finalCpuUsage << "%\n";

    // Вычисление среднего значения нагрузки на CPU
    double averageCpuUsage = std::accumulate(cpuUsageValues.begin(), cpuUsageValues.end(), 0.0) / cpuUsageValues.size();
    std::cout << "Average CPU load during execution: " << averageCpuUsage << "%\n";

    return 0;
}
