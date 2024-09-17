#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>

// Имя файла для хранения контекста
const char* CONTEXT_FILE = "process_context.txt";
const char* STOP_EVENT_NAME = "Global\\StopProcessEvent";

HWND hDataLabel = NULL;  // Глобальная переменная для метки отображения данных

// Функция для сохранения состояния процесса в файл
void SaveContextToFile(int processData) {
    std::ofstream file(CONTEXT_FILE);
    if (file.is_open()) {
        file << processData;
        file.close();
        std::cout << "Context saved to file.\n";
    }
}

// Функция для загрузки состояния процесса из файла
int LoadContextFromFile() {
    std::ifstream file(CONTEXT_FILE);
    int processData = 0;
    if (file.is_open()) {
        file >> processData;
        file.close();
        std::cout << "Context loaded from file.\n";
    }
    return processData;
}

// Функция для обновления отображаемого числа в окне
void UpdateDataLabel(int processData) {
    char buffer[256];
    sprintf(buffer, "Process data: %d", processData);
    SetWindowText(hDataLabel, buffer);
}

// Функция для запуска новой копии процесса
void RestartProcess() {
    TCHAR szPath[MAX_PATH];
    GetModuleFileName(NULL, szPath, MAX_PATH);

    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcess(szPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "New process started.\n";
    } else {
        std::cerr << "Failed to start new process.\n";
    }
}

// Основная функция окна
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int processData = 0; // Пример данных для сохранения
    static HANDLE stopEvent = NULL; // Событие для завершения процесса

    switch (msg) {
        case WM_CREATE:
            // Загружаем контекст при старте процесса
            processData = LoadContextFromFile();

            // Создаем или открываем глобальное событие для завершения процесса
            stopEvent = CreateEventA(NULL, TRUE, FALSE, STOP_EVENT_NAME);
            if (stopEvent == NULL) {
                std::cerr << "Failed to create/open stop event: " << GetLastError() << std::endl;
                PostQuitMessage(0); // Завершаем процесс, если не удалось создать событие
            }

            // Создаём статический элемент для отображения данных
            hDataLabel = CreateWindow("STATIC", "Process data: 0", WS_VISIBLE | WS_CHILD, 50, 100, 200, 24, hwnd, NULL, GetModuleHandle(NULL), NULL);

            // Инициализируем значение метки
            UpdateDataLabel(processData);
            break;

        case WM_CLOSE:
            // Перед закрытием сохраняем контекст и запускаем новый процесс
            SaveContextToFile(processData);
            RestartProcess();
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            // Освобождаем ресурсы
            if (stopEvent != NULL) {
                CloseHandle(stopEvent);
            }
            PostQuitMessage(0);
            break;

        case WM_COMMAND:
            // Обработка команды: например, увеличиваем данные
            if (LOWORD(wParam) == 1) {
                processData++;
                UpdateDataLabel(processData);  // Обновляем текст в окне
            }
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Главная функция WinAPI
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* className = "SelfRestoringProcess";

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_VREDRAW | CS_HREDRAW, WndProc, 0, 0, hInstance, NULL, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW+1), NULL, className, NULL };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, className, "Self-Restoring Process", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Добавляем кнопку для изменения состояния процесса
    CreateWindow("BUTTON", "Increase Data", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 50, 50, 150, 24, hwnd, (HMENU)1, hInstance, NULL);

    MSG msg = {0};

    // Главный цикл программы
    while (true) {
        // Проверка на событие завершения процесса
        HANDLE stopEvent = OpenEventA(SYNCHRONIZE, FALSE, STOP_EVENT_NAME);
        if (stopEvent != NULL) {
            DWORD waitResult = WaitForSingleObject(stopEvent, 0);
            if (waitResult == WAIT_OBJECT_0) {
                std::cout << "Stop event detected. Exiting process.\n";
                break; // Завершаем цикл и процесс
            }
        }

        // Обрабатываем сообщения окна
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}
