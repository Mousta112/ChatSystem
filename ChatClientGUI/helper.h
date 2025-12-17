#pragma once
#include <rpcasync.h>
#include <wtypes.h>
#pragma once

void AppendText(HWND hEdit, const wstring& text);
void DoDisconnect();
void ReceiverThread(HWND hwnd);
