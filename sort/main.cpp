#include <windows.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <numeric>
#include <Pdh.h>
#include <atomic>

std::mutex coutMutex;
std::atomic<bool> monitoringCpu(true);

class CpuMonitor {
public:
    CpuMonitor() {
        PdhOpenQuery(NULL, 0, &cpuQuery);
        PdhAddEnglishCounterW(cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
        PdhCollectQueryData(cpuQuery);
        Sleep(1000);
    }

    ~CpuMonitor() {
        PdhCloseQuery(cpuQuery);
    }

    double getCpuUsage() {
        PDH_FMT_COUNTERVALUE counterVal;
        PdhCollectQueryData(cpuQuery);
        Sleep(1000);
        PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
        return counterVal.doubleValue;
    }

private:
    PDH_HQUERY cpuQuery{};
    PDH_HCOUNTER cpuTotal{};
};



DWORD WINAPI monitorCpuUsage(LPVOID param) {
    auto *cpuMonitor = static_cast<CpuMonitor *>(param);
    std::vector<double> cpuUsageValues;

    while (monitoringCpu) {
        double cpuUsage = cpuMonitor->getCpuUsage();
        {
            std::lock_guard<std::mutex> lock(coutMutex);
            cpuUsageValues.push_back(cpuUsage);
        }
        Sleep(1000);
    }

    double averageCpuUsage = std::accumulate(cpuUsageValues.begin(), cpuUsageValues.end(), 0.0) / cpuUsageValues.size();
    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "Average CPU load during execution: " << averageCpuUsage << "%\n";

    return 0;
}

void merge(int *arr, int left, int mid, int right) {
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

struct ThreadTaskData {
    int *array;
    int left;
    int mid;
    int right;
    int threadId;
};

DWORD WINAPI threadSort(LPVOID param) {
    auto *data = static_cast<ThreadTaskData *>(param);
    std::sort(data->array + data->left, data->array + data->right + 1);

    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "\nThread " << data->threadId << ": done\n";
    return 0;
}

DWORD WINAPI threadMerge(LPVOID param) {
    auto *data = static_cast<ThreadTaskData *>(param);
    merge(data->array, data->left, data->mid, data->right);

    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "\nThread " << data->threadId << ": merged " << data->left << " to " << data->right << "\n";
    return 0;
}

void parallelMerge(int *arr, int size, int numThreads, std::vector<HANDLE>& threads) {
    int chunkSize = size / numThreads;
    std::vector<ThreadTaskData> threadData(numThreads);

    for (int mergeSize = chunkSize; mergeSize < size; mergeSize *= 2) {
        int currentThreads = numThreads / 2;
        threads.resize(currentThreads);
        threadData.resize(currentThreads);

        for (int i = 0; i < currentThreads; ++i) {
            int left = i * 2 * mergeSize;
            int mid = std::min(left + mergeSize - 1, size - 1);
            int right = std::min(left + 2 * mergeSize - 1, size - 1);

            threadData[i] = {arr, left, mid, right, i};
            threads[i] = CreateThread(NULL, 0, threadMerge, &threadData[i], 0, NULL);
        }

        WaitForMultipleObjects(currentThreads, threads.data(), TRUE, INFINITE);
        for (HANDLE &thread: threads) CloseHandle(thread);

        numThreads /= 2;
    }
}

void parallelSort(int *arr, int size, int numThreads, std::vector<HANDLE>& threads) {
    std::vector<ThreadTaskData> threadData(numThreads);

    int chunkSize = size / numThreads;


    for (int i = 0; i < numThreads; ++i) {
        int left = i * chunkSize;
        int right = (i == numThreads - 1) ? size - 1 : (left + chunkSize - 1);
        threadData[i] = {arr, left, 0, right, i}; // mid не используется для сортировки
        threads[i] = CreateThread(NULL, 0, threadSort, &threadData[i], 0, NULL);
    }

    WaitForMultipleObjects(numThreads, threads.data(), TRUE, INFINITE);
}

void sortAndMerge(int *arr, int size, int numThreads) {
    std::vector<HANDLE> threads(numThreads);

    parallelSort(arr, size, numThreads, threads);
    parallelMerge(arr, size, numThreads, threads);
}


int main() {
    int size = 1'000'000'000, numThreads = 0;
    std::cout << "Array size: " << size << "\n";
    std::cin >> size;

    while (numThreads <= 0 || numThreads > 64) {
        std::cout << "Number of threads (1-64): ";
        std::cin >> numThreads;
    }


    std::vector<int> array(size);
    for (int j = 0; j < size; j++) array[j] = j % 10;

    CpuMonitor cpuMonitor;

    double initialCpuUsage = cpuMonitor.getCpuUsage();
    std::cout << "CPU load before start: " << initialCpuUsage << "%\n";

    HANDLE cpuMonitorThread = CreateThread(NULL, 0, monitorCpuUsage, &cpuMonitor, 0, NULL);

    auto start = std::chrono::high_resolution_clock::now();
    sortAndMerge(array.data(), size, numThreads);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> duration = end - start;
    std::cout << "\nExecution time: " << duration.count() << " seconds\n";

    monitoringCpu = false;

    WaitForSingleObject(cpuMonitorThread, INFINITE);
    CloseHandle(cpuMonitorThread);

    double finalCpuUsage = cpuMonitor.getCpuUsage();
    std::cout << "CPU load after execution: " << finalCpuUsage << "%\n";

//    for (int i = 0; i < size;) {
//        std:: cout << array[i] << " ";
//        i = i + 10000;
//    }

    return 0;
}
