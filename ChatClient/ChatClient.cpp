#include <iostream>
#include <string>
#include <thread>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

void receiveMessages(SOCKET sock)
{
    char buf[4096];

    while (true)
    {
        ZeroMemory(buf, 4096);
        int bytesReceived = recv(sock, buf, 4096, 0);

        if (bytesReceived <= 0)
        {
            cout << "\n[Info] Disconnected from server.\n";
            break;
        }

        string msg(buf, 0, bytesReceived);
        cout << "\n" << msg << "\n> ";
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

    // 2) Get IP and Port from user input
    string ipAddress;
    string portStr;
    cout << "Enter server IP (default: 127.0.0.1): ";
    getline(cin, ipAddress);
    if (ipAddress.empty()) {
        ipAddress = "127.0.0.1";  // Default to localhost
    }

    cout << "Enter server port (default: 54000): ";
    getline(cin, portStr);
    if (portStr.empty()) {
        portStr = "54000";  // Default port
    }
    int port = stoi(portStr);  // Convert port from string to int

    // 3) Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cout << "Can't create socket!" << endl;
        WSACleanup();
        return 0;
    }

    // 4) Server info
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, ipAddress.c_str(), &server.sin_addr);

    // 5) Connect
    int conn = connect(sock, (sockaddr*)&server, sizeof(server));
    if (conn == SOCKET_ERROR) {
        cout << "Can't connect to server!" << endl;
        closesocket(sock);
        WSACleanup();
        return 0;
    }

    cout << "Connected to server!\n";

    // 6) Enter username and send as first message
    string username;
    cout << "Enter your username: ";
    getline(cin, username);

    if (username.empty())
        username = "User";

    send(sock, username.c_str(), (int)username.size(), 0);

    // 7) Start receiver thread
    thread receiver(receiveMessages, sock);

    // 8) Loop to send messages
    string userInput;

    while (true)
    {
        cout << "> ";
        getline(cin, userInput);

        if (userInput == "quit")
            break;

        if (userInput.empty())
            continue;

        int sendResult = send(sock, userInput.c_str(), (int)userInput.size(), 0);
        if (sendResult == SOCKET_ERROR)
        {
            cout << "Error sending message.\n";
            break;
        }
    }

    // 9) Cleanup
    closesocket(sock);
    if (receiver.joinable())
        receiver.join();

    WSACleanup();
    return 0;
}
