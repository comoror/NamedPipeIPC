// PipeServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "../CNamedPipeIPC/CNamedPipeIPC.h"

std::vector<DWORD> g_vPipeInstances;
HANDLE g_hEventConnected = NULL;

VOID OnConnected(DWORD pipeIndex)
{
	printf("Client connected: %d\n", pipeIndex);

	//add the pipe index to the vector
	g_vPipeInstances.push_back(pipeIndex);

	SetEvent(g_hEventConnected);
}

VOID OnDisconnected(DWORD pipeIndex)
{
	printf("Client disconnected: %d\n", pipeIndex);

	//remove the pipe index from the vector
	g_vPipeInstances.erase(std::remove(g_vPipeInstances.begin(), g_vPipeInstances.end(), pipeIndex), g_vPipeInstances.end());
}

//callback function to process the message from the client
VOID OnPipeMessage(DWORD pipeIndex, MemBuffer* rqstBuf, MemBuffer* rspBuf)
{
	//print the message from the client
	printf("Received message from client %d: %s\n", pipeIndex, (char*)rqstBuf->AccessMem());

	//just echo the message back to the client
	char msg[256] = { 0 };
	sprintf_s(msg, "Replay from server, %s", (char*)rqstBuf->AccessMem());
	rspBuf->AddItem((LPVOID)msg, strlen(msg) + 1);
}
int main()
{
	g_hEventConnected = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (g_hEventConnected == NULL)
	{
		printf("Failed to create event\n");
		return -1;
	}

	CNamedPipeServer* pPipeServer = new CNamedPipeServer(_T("\\\\.\\pipe\\testpipe"),
		OnPipeMessage,
		OnConnected,
		OnDisconnected);

	//start a thread to run the pipe server
	std::thread t1([=]() {
		pPipeServer->Run();
		});

	WaitForSingleObject(g_hEventConnected, INFINITE);

	////send a message to the client
	//for (auto pipeIndex : g_vPipeInstances)
	//{
	//	char msg[256] = { 0 };
	//	sprintf_s(msg, "Hello from server, your index is %d", pipeIndex);
	//	pPipeServer->SendMessage(pipeIndex, (LPVOID)msg, strlen(msg) + 1);
	//}

	//

	int count = 0;
	while (count++ < 100)
	{
		//broadcast a message to all clients
		char msg2[256] = { 0 };
		sprintf_s(msg2, "Hello from server, index: %d", count);
		pPipeServer->BroadcastMessage((LPVOID)msg2, strlen(msg2) + 1);
		Sleep(rand() % 10);
	}
	//press any key to exit
	//system("PAUSE");
	Sleep(2000);

	//stop the pipe server
	pPipeServer->Stop();

	//wait for the thread to finish
	if (t1.joinable())
		t1.join();

	delete pPipeServer;
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
