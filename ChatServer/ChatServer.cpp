#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

vector<SOCKET> clients;                     // كل السوكيتس بتاعة الكلاينتس
unordered_map<SOCKET, string> clientNames;  // socket -> username
mutex clientsMutex;

// رسالة من السيرفر نفسه (مثلاً joined / left)
void broadcastServerMessage(const string& msg)
{
    lock_guard<mutex> lock(clientsMutex);

    string fullMsg = "[*] " + msg;  // شكلها قدام الكلاينتس

    for (SOCKET client : clients)
    {
        send(client, fullMsg.c_str(), (int)fullMsg.size(), 0);
    }
}

// رسالة جاية من يوزر معين
void broadcastUserMessage(const string& msg, SOCKET sender)
{
    lock_guard<mutex> lock(clientsMutex);

    string name = "Unknown";
    auto it = clientNames.find(sender);
    if (it != clientNames.end())
        name = it->second;

    string fullMsg = "[" + name + "]: " + msg;

    for (SOCKET client : clients)
    {
        if (client == sender) continue; // لو عايز توصل للمرسل نفسه شيل السطر ده
        send(client, fullMsg.c_str(), (int)fullMsg.size(), 0);
    }
}

void handleClient(SOCKET clientSocket)
{
    char buf[4096];
    bool hasName = false;
    string clientName = "";

    while (true)
    {
        ZeroMemory(buf, 4096);
        int bytesReceived = recv(clientSocket, buf, 4096, 0);

        if (bytesReceived <= 0)
        {
            // disconnect
            {
                lock_guard<mutex> lock(clientsMutex);
                clients.erase(
                    remove(clients.begin(), clients.end(), clientSocket),
                    clients.end()
                );
                clientNames.erase(clientSocket);
            }

            if (!clientName.empty())
            {
                cout << clientName << " disconnected." << endl;
                broadcastServerMessage(clientName + " left the chat.");
            }
            else
            {
                cout << "Client disconnected." << endl;
            }

            closesocket(clientSocket);
            break;
        }

        string msg(buf, 0, bytesReceived);

        if (!hasName)
        {
            // أول رسالة = اسم اليوزر
            clientName = msg;

            {
                lock_guard<mutex> lock(clientsMutex);
                clientNames[clientSocket] = clientName;
            }

            cout << "User '" << clientName << "' joined." << endl;
            broadcastServerMessage(clientName + " joined the chat.");
            hasName = true;
        }
        else
        {
            cout << "[" << clientName << "]: " << msg << endl;
            broadcastUserMessage(msg, clientSocket);
        }
    }
}

int main()
{
    // 1) Start Winsock
    WSADATA wsData;
    WORD ver = MAKEWORD(2, 2);
    int wsOK = WSAStartup(ver, &wsData);
    if (wsOK != 0) {
        cout << "Can't initialize Winsock!" << endl;
        return 0;
    }

    // 2) Create listening socket
    SOCKET listening = socket(AF_INET, SOCK_STREAM, 0);
    if (listening == INVALID_SOCKET) {
        cout << "Can't create listening socket!" << endl;
        WSACleanup();
        return 0;
    }

    // 3) Bind
    sockaddr_in hint;
    hint.sin_family = AF_INET;
    hint.sin_port = htons(54000);
    hint.sin_addr.S_un.S_addr = INADDR_ANY;

    bind(listening, (sockaddr*)&hint, sizeof(hint));

    // 4) Listen
    listen(listening, SOMAXCONN);
    cout << "Server is running and waiting for clients..." << endl;

    // 5) Accept loop
    while (true)
    {
        sockaddr_in client;
        int clientSize = sizeof(client);

        SOCKET clientSocket = accept(listening, (sockaddr*)&client, &clientSize);
        if (clientSocket == INVALID_SOCKET)
        {
            cout << "Failed to accept client!" << endl;
            continue;
        }

        cout << "New client connected (waiting for username)..." << endl;

        {
            lock_guard<mutex> lock(clientsMutex);
            clients.push_back(clientSocket);
        }

        thread t(handleClient, clientSocket);
        t.detach();
    }

    closesocket(listening);
    WSACleanup();
    return 0;
}
