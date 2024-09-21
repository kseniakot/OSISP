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
    auto* cpuMonitor = static_cast<CpuMonitor*>(param);
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

struct ThreadTaskData {
    int* array;
    int left;
    int mid;
    int right;
    int threadId;
    PTP_WORK workItem;
};

VOID CALLBACK threadSort(PTP_CALLBACK_INSTANCE, PVOID param, PTP_WORK) {
    auto* data = static_cast<ThreadTaskData*>(param);
    std::sort(data->array + data->left, data->array + data->right + 1);

    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "\nThread " << data->threadId << ": done sorting\n";
}

VOID CALLBACK threadMerge(PTP_CALLBACK_INSTANCE, PVOID param, PTP_WORK) {
    auto* data = static_cast<ThreadTaskData*>(param);
    merge(data->array, data->left, data->mid, data->right);

    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << "\nThread " << data->threadId << ": merged " << data->left << " to " << data->right << "\n";
}

void parallelMerge(int* arr, int size, int numThreads, std::vector<PTP_WORK>& workItems, std::vector<ThreadTaskData>& threadData) {
    int chunkSize = size / numThreads;

    for (int mergeSize = chunkSize; mergeSize < size; mergeSize *= 2) {
        int currentThreads = numThreads / 2;
        for (int i = 0; i < currentThreads; ++i) {
            int left = i * 2 * mergeSize;
            int mid = std::min(left + mergeSize - 1, size - 1);
            int right = std::min(left + 2 * mergeSize - 1, size - 1);

            threadData[i] = {arr, left, mid, right, i, nullptr};
            workItems[i] = CreateThreadpoolWork(threadMerge, &threadData[i], nullptr);
            SubmitThreadpoolWork(workItems[i]);
        }

        // Wait for all merging work items to complete
        for (int i = 0; i < currentThreads; ++i) {
            WaitForThreadpoolWorkCallbacks(workItems[i], FALSE);
            CloseThreadpoolWork(workItems[i]);
        }

        numThreads /= 2;
    }
}

void parallelSort(int* arr, int size, int numThreads, std::vector<PTP_WORK>& workItems, std::vector<ThreadTaskData>& threadData) {
    int chunkSize = size / numThreads;

    for (int i = 0; i < numThreads; ++i) {
        int left = i * chunkSize;
        int right = (i == numThreads - 1) ? size - 1 : (left + chunkSize - 1);
        threadData[i] = {arr, left, 0, right, i, nullptr};
        workItems[i] = CreateThreadpoolWork(threadSort, &threadData[i], nullptr);
        SubmitThreadpoolWork(workItems[i]);
    }

    // Wait for all sorting work items to complete
    for (int i = 0; i < numThreads; ++i) {
        WaitForThreadpoolWorkCallbacks(workItems[i], FALSE);
        CloseThreadpoolWork(workItems[i]);
    }
}

void sortAndMerge(int* arr, int size, int numThreads) {
    std::vector<PTP_WORK> workItems(numThreads);
    std::vector<ThreadTaskData> threadData(numThreads);

    parallelSort(arr, size, numThreads, workItems, threadData);
    parallelMerge(arr, size, numThreads, workItems, threadData);
}

template<typename T>
void getInput(T& variable, const std::string& prompt, T minValue, T maxValue) {
    while (true) {
        std::cout << prompt;
        std::cin >> variable;

        if (std::cin.fail() || variable < minValue || variable > maxValue) {
            std::cout << "Invalid input. Please enter a value between " << minValue << " and " << maxValue << ".\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else {
            break;
        }
    }
}

int main() {
    int size = 0, numThreads = 0;
    getInput(size, "Enter array size (0 - 2'000'000'000): ", 1, 2'000'000'000);
    getInput(numThreads, "Enter number of threads (1 - 64): ", 1, 64);

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

    return 0;
}
