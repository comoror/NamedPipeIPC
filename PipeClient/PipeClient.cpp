// PipeClient.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "../CNamedPipeIPC/CNamedPipeClient.h"

VOID OnConnect()
{
	printf("Connected to the pipe server\n");
}

VOID OnDisconnect()
{
	printf("Disconnected from the pipe server\n");
}

VOID OnMessage(MemBuffer* InBuf)
{
	printf("Received message from server: %s\n", (char*)InBuf->AccessMem());
}

int main()
{
	CNamedPipeClient* pPipeClient = new CNamedPipeClient();

	//connect to the pipe server
	DWORD dwErr = pPipeClient->Connect(TEXT("\\\\.\\pipe\\testpipe"), OnMessage, OnConnect, OnDisconnect);
	if (dwErr != 0)
	{
		printf("Failed to connect to the pipe server, error code: %d\n", dwErr);
	}

	//send a message to the server
	int count = 0;
	while (count++ < 100)
	{
		char msg[256] = { 0 };
		sprintf_s(msg, "Hello from client, index: %d", count);
		pPipeClient->SendMessage((LPVOID)msg, strlen(msg) + 1);
		Sleep(rand() % 10);
	}

	//wait for the message from the server
	Sleep(1000);

	//disconnect from the pipe server
	pPipeClient->Disconnect();

	delete pPipeClient;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
