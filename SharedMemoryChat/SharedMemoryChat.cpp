#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstring>

using namespace std;

// اسم الـ Shared Memory
const wchar_t* SHM_NAME = L"Local\\ChatSharedMemory_OS_Project";
// اسم الـ Semaphore (نستخدمه كـ Mutex)
const wchar_t* SEM_MUTEX_NAME = L"Local\\Chat_SharedMutex";

// حجم الرسالة والـ history
const int MSG_SIZE = 256;
const int MAX_MESSAGES = 1024;

// كل رسالة في الشات
struct ChatMessage
{
    char sender[32];       // اسم المرسل
    char text[MSG_SIZE];   // محتوى الرسالة
};

// البيانات المشتركة بين كل الـ Users
struct SharedData
{
    ChatMessage messages[MAX_MESSAGES];
    int messageCount;
};

bool g_running = true;

// Thread الاستقبال: يراقب الـ shared memory ويطبع أي رسائل جديدة من الآخرين
void receiverThreadFunc(SharedData* shared, HANDLE hMutex, const string& myName)
{
    int lastSeen = 0;

    while (g_running)
    {
        vector<ChatMessage> newMessages;

        // نقفل الـ mutex وننسخ الرسائل الجديدة محلياً
        DWORD res = WaitForSingleObject(hMutex, INFINITE);
        if (res != WAIT_OBJECT_0)
            break;

        int total = shared->messageCount;
        for (int i = lastSeen; i < total; ++i)
        {
            newMessages.push_back(shared->messages[i]);
        }
        lastSeen = total;

        ReleaseSemaphore(hMutex, 1, nullptr);

        // نطبع الرسائل بعد ما نسيب الـ mutex
        for (auto& msg : newMessages)
        {
            // تجاهل رسائل نفسك (علشان ما تتكررّش)
            if (strcmp(msg.sender, myName.c_str()) == 0)
                continue;

            cout << "\n[" << msg.sender << "]: " << msg.text << "\n> ";
            cout.flush();
        }

        Sleep(100); // تهدئة بسيطة عشان مانبقاش busy-wait
    }
}

int main()
{
    cout << "=== Shared Memory Chat (Multi-User, OS Project) ===\n";

    // ناخد Username من اليوزر
    cout << "Enter your username: ";
    string username;
    getline(cin, username);

    if (username.empty())
        username = "User";

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

    bool firstCreator = (GetLastError() != ERROR_ALREADY_EXISTS);

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

    // لو أول واحد يخلق الـ shared memory نعمل تهيئة
    if (firstCreator)
    {
        ZeroMemory(shared, sizeof(SharedData));
        shared->messageCount = 0;
    }

    // 2) إنشاء أو فتح الـ Semaphore (Mutex)
    HANDLE hMutex = CreateSemaphoreW(
        nullptr,
        1,               // initial count = 1 (مفتوح)
        1,               // max count    = 1 (Mutex)
        SEM_MUTEX_NAME
    );

    if (!hMutex)
    {
        cerr << "Failed to create/open semaphore. Error: " << GetLastError() << endl;
        if (shared) UnmapViewOfFile(shared);
        if (hMapFile) CloseHandle(hMapFile);
        return 1;
    }

    cout << "\nWelcome, " << username << "!\n";
    cout << "Type messages and press Enter to send.\n";
    cout << "Type 'quit' to exit.\n\n";

    // 3) نرسل رسالة "joined" كـ system message
    {
        string joinedMsg = username + " joined the chat.";

        WaitForSingleObject(hMutex, INFINITE);

        int idx = shared->messageCount % MAX_MESSAGES;
        strncpy_s(shared->messages[idx].sender, username.c_str(), _TRUNCATE);
        strncpy_s(shared->messages[idx].text, joinedMsg.c_str(), _TRUNCATE);
        shared->messageCount++;

        ReleaseSemaphore(hMutex, 1, nullptr);
    }

    // 4) تشغيل Thread الاستقبال
    thread receiver(receiverThreadFunc, shared, hMutex, username);

    string line;
    while (true)
    {
        cout << "> ";
        getline(cin, line);

        if (line.empty())
            continue;

        // لو المستخدم عايز يخرج
        if (line == "quit" || line == "QUIT")
        {
            g_running = false;

            // نبعث رسالة system إنه خرج
            string leftMsg = username + " left the chat.";

            WaitForSingleObject(hMutex, INFINITE);

            int idx = shared->messageCount % MAX_MESSAGES;
            strncpy_s(shared->messages[idx].sender, username.c_str(), _TRUNCATE);
            strncpy_s(shared->messages[idx].text, leftMsg.c_str(), _TRUNCATE);
            shared->messageCount++;

            ReleaseSemaphore(hMutex, 1, nullptr);

            cout << "Exiting chat...\n";
            break;
        }

        // كتابة الرسالة في الـ shared memory
        WaitForSingleObject(hMutex, INFINITE);

        int idx = shared->messageCount % MAX_MESSAGES;
        strncpy_s(shared->messages[idx].sender, username.c_str(), _TRUNCATE);
        strncpy_s(shared->messages[idx].text, line.c_str(), _TRUNCATE);
        shared->messageCount++;

        ReleaseSemaphore(hMutex, 1, nullptr);

        // نطبع الرسالة عندك مرة واحدة بس
        cout << "You: " << line << "\n";
    }

    // 5) Cleanup
    if (receiver.joinable())
        receiver.join();

    if (shared) UnmapViewOfFile(shared);
    if (hMapFile) CloseHandle(hMapFile);
    if (hMutex) CloseHandle(hMutex);

    return 0;
}
