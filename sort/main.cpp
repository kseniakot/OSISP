#include <windows.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <Pdh.h>
#include <PdhMsg.h>

std::mutex coutMutex;

static PDH_HQUERY cpuQuery;
static PDH_HCOUNTER cpuTotal;


void initCpuQuery() {
    PdhOpenQuery(NULL, 0, &cpuQuery);
    PdhAddEnglishCounterW(cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &cpuTotal);
    PdhCollectQueryData(cpuQuery);
    Sleep(5000);
}

// Function to get the current CPU usage
double getCpuUsage() {
    initCpuQuery();
    PDH_FMT_COUNTERVALUE counterVal;
    PdhCollectQueryData(cpuQuery);
    PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
    return counterVal.doubleValue;
}

struct ThreadData {
    int* array;
    int left;
    int right;
    bool completed = false; // Flag to indicate sorting completion
    int threadId;  // Thread identifier
};

// Function to be executed by each thread
DWORD WINAPI threadSort(LPVOID param) {
    ThreadData* data = (ThreadData*)param;

    // Get initial CPU usage
    double initialCpuUsage = getCpuUsage();

    // Sort array segment
    std::sort(data->array + data->left, data->array + data->right + 1);

    {
        std::lock_guard<std::mutex> lock(coutMutex);
        std::cout << "Thread " << data->threadId << ": done\n";
        double finalCpuUsage = getCpuUsage();
        std::cout << "Thread " << data->threadId << ": CPU load before: " << initialCpuUsage << "%, after: " << finalCpuUsage << "%\n";
    }

    data->completed = true;  // Mark sorting as completed
    return 0;
}

// Merge sorted array segments
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

// Parallel sort function
void parallelSort(int* arr, int size, int numThreads) {
    std::vector<HANDLE> threads(numThreads);
    std::vector<ThreadData> threadData(numThreads);

    int chunkSize = size / numThreads;

    // Start threads for sorting array segments
    for (int i = 0; i < numThreads; ++i) {
        int left = i * chunkSize;
        int right = (i == numThreads - 1) ? size - 1 : (left + chunkSize - 1);

        threadData[i] = { arr, left, right, 0 };  // Thread data initialization
        threadData[i].threadId = i;  // Set thread identifier
        threads[i] = CreateThread(NULL, 0, threadSort, &threadData[i], 0, NULL);
    }

    // Wait for all sorting threads to complete
    WaitForMultipleObjects(numThreads, threads.data(), TRUE, INFINITE);

    // Close thread handles
    for (HANDLE& thread : threads) {
        CloseHandle(thread);
    }

    // Merge sorted array segments
    for (int sizeMerge = chunkSize; sizeMerge < size; sizeMerge *= 2) {
        for (int i = 0; i < size; i += 2 * sizeMerge) {
            int mid = std::min(i + sizeMerge - 1, size - 1);
            int right = std::min(i + 2 * sizeMerge - 1, size - 1);
            merge(arr, i, mid, right);
        }
    }
}

int main() {
    int size = 1'000'000, numThreads = 4;

    std::cout << "Array size: " << size << "\n";
    std::cout << "Number of threads: " << numThreads << "\n";

    if (numThreads < 1 || size < numThreads) {
        std::cout << "Invalid number of threads or array size.\n";
        return -1;
    }

    // Initialize array
    std::vector<int> array(size);
    for (int j = 0; j < size; j++) {
        array[j] = j % 10;  // Fill array for testing
    }

    // Initialize CPU usage query
    initCpuQuery();

    double initialCpuUsage = getCpuUsage();
    std::cout << "CPU load before start: " << initialCpuUsage << "%\n";

    // Measure time taken for sorting
    auto start = std::chrono::high_resolution_clock::now();

    parallelSort(array.data(), size, numThreads);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout << "\nExecution time: " << duration.count() << " seconds\n";

    double finalCpuUsage = getCpuUsage();
    std::cout << "CPU load after execution: " << finalCpuUsage << "%\n";

    return 0;
}
