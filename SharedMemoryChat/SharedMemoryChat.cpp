#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>

using namespace std;

// اسم الـ Shared Memory
const wchar_t* SHM_NAME = L"Local\\ChatSharedMemory_OS_Project";
// أسماء السيمفورز
const wchar_t* SEM_A2B_NAME = L"Local\\Chat_Sem_A2B";
const wchar_t* SEM_B2A_NAME = L"Local\\Chat_Sem_B2A";

// حجم الرسالة
const int MSG_SIZE = 256;

// البيانات اللي هتتبعت بين الـ Processes
struct SharedData
{
    char msgAtoB[MSG_SIZE]; // رسالة من A إلى B
    char msgBtoA[MSG_SIZE]; // رسالة من B إلى A
};

void receiverThreadFunc(bool isUserA, SharedData* shared, HANDLE semFromOther)
{
    while (true)
    {
        // مستني رسالة من الطرف التاني
        DWORD res = WaitForSingleObject(semFromOther, INFINITE);
        if (res != WAIT_OBJECT_0)
            break;

        string received;
        if (isUserA)
        {
            // A بيستقبل من B
            received = string(shared->msgBtoA);
        }
        else
        {
            // B بيستقبل من A
            received = string(shared->msgAtoB);
        }

        if (received == "quit" || received == "QUIT")
        {
            cout << "\n[Other user disconnected]\n";
            break;
        }

        cout << "\n[Other]: " << received << "\n> ";
        cout.flush();
    }
}

int main()
{
    cout << "=== Shared Memory Chat (OS Project) ===\n";
    cout << "Choose your role:\n";
    cout << "1) User A\n";
    cout << "2) User B\n";
    cout << "Enter choice: ";

    int choice;
    cin >> choice;
    cin.ignore(); // عشان نشيل الـ '\n' من الـ input

    bool isUserA = (choice == 1);

    cout << (isUserA ? "[You are User A]\n" : "[You are User B]\n");

    // 1) إنشاء أو فتح الـ Shared Memory
    HANDLE hMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,     // استخدام الـ Pagefile
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedData),
        SHM_NAME
    );

    if (!hMapFile)
    {
        cerr << "Failed to create/open file mapping. Error: " << GetLastError() << endl;
        return 1;
    }

    SharedData* shared = (SharedData*)MapViewOfFile(
        hMapFile,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        sizeof(SharedData)
    );

    if (!shared)
    {
        cerr << "Failed to map view of file. Error: " << GetLastError() << endl;
        CloseHandle(hMapFile);
        return 1;
    }

    // 2) إنشاء أو فتح السيمفورز
    HANDLE hSemA2B = CreateSemaphoreW(
        nullptr,
        0,              // initial count (0 = مفيش رسالة لسه)
        100,
        SEM_A2B_NAME
    );

    HANDLE hSemB2A = CreateSemaphoreW(
        nullptr,
        0,
        100,
        SEM_B2A_NAME
    );

    if (!hSemA2B || !hSemB2A)
    {
        cerr << "Failed to create/open semaphores. Error: " << GetLastError() << endl;
        if (shared) UnmapViewOfFile(shared);
        if (hMapFile) CloseHandle(hMapFile);
        return 1;
    }

    // 3) تشغيل Thread الاستقبال
    HANDLE semFromOther = isUserA ? hSemB2A : hSemA2B;
    thread receiver(receiverThreadFunc, isUserA, shared, semFromOther);

    cout << "Type messages and press Enter to send.\n";
    cout << "Type 'quit' to exit.\n\n";

    string line;

    while (true)
    {
        cout << "> ";
        getline(cin, line);

        if (isUserA)
        {
            // A بيبعت إلى B
            memset(shared->msgAtoB, 0, MSG_SIZE);
            strncpy_s(shared->msgAtoB, MSG_SIZE, line.c_str(), _TRUNCATE);

            // نبلغ الـ B إن فيه رسالة
            ReleaseSemaphore(hSemA2B, 1, nullptr);
        }
        else
        {
            // B بيبعت إلى A
            memset(shared->msgBtoA, 0, MSG_SIZE);
            strncpy_s(shared->msgBtoA, MSG_SIZE, line.c_str(), _TRUNCATE);

            // نبلغ الـ A إن فيه رسالة
            ReleaseSemaphore(hSemB2A, 1, nullptr);
        }

        if (line == "quit" || line == "QUIT")
        {
            cout << "Exiting chat...\n";
            break;
        }
    }

    // 4) Cleanup
    if (receiver.joinable())
        receiver.join();

    if (shared) UnmapViewOfFile(shared);
    if (hMapFile) CloseHandle(hMapFile);
    if (hSemA2B) CloseHandle(hSemA2B);
    if (hSemB2A) CloseHandle(hSemB2A);

    return 0;
}
