
在桌面开发中，常常需要用到进程间通信（Inter-Process Communication, IPC），而最常使用的方式便是命名管道。

Microsoft Docs： https://learn.microsoft.com/en-us/windows/win32/ipc/using-pipes

微软官方文档给出了命名管道服务端的三种使用方法，分别是：
1. 多线程方式（Multithreaded）
2. 重叠IO方式（Overlapped IO）
3. 完成例程方式（Completion Routinues）

具体分析参见：https://zhuanlan.zhihu.com/p/953624404

上面微软官方的三种服务端示例方式都仅支持“一问一答”的方式，**服务端不能主动向客户端发送数据**。


服务端主动向客户端发起单播或者广播，在IPC中也经常存在类似的需求。下面介绍一种基于重叠IO方式的，支持服务端单播和广播的命名管道使用方法。

与微软示例的重叠IO方式不同之处在于：**异步写入操作不在“等待事件”列表中**。
同时，客户端为了能收到数据，也采用和服务端一致的重叠IO方式。

### 服务端使用
```C++
//callback function to process the message from the client
VOID OnPipeMessage(DWORD pipeIndex, MemBuffer* rqstBuf, MemBuffer* rspBuf)
{
	//print the message from the client
	printf("client %d: %s\n", pipeIndex, (char*)rqstBuf->AccessMem());

	//optional： just echo the message back to the client
	char msg[256] = { 0 };
	sprintf_s(msg, "Replay from server, %s", (char*)rqstBuf->AccessMem());
	rspBuf->AddItem((LPVOID)msg, strlen(msg) + 1);
}

int main()
{
	CNamedPipeServer* pPipeServer = new CNamedPipeServer(_T("\\\\.\\pipe\\testpipe"),
		OnPipeMessage);

	//start a thread to run the pipe server
	std::thread t1([=]() {
		pPipeServer->Run();
		});

        //press any key to send and broadcase
	system("PAUSE");

	//send a message to the client
	for (auto pipeIndex : g_vPipeInstances)
	{
		char msg[256] = { 0 };
		sprintf_s(msg, "Hello from server, your index is %d", pipeIndex);
		pPipeServer->SendMessage(pipeIndex, (LPVOID)msg, strlen(msg) + 1);
	}
	
        //broadcast a message to all clients
	char msg2[256] = { 0 };
	sprintf_s(msg2, "Broadcase from server.");
	pPipeServer->BroadcastMessage((LPVOID)msg2, strlen(msg2) + 1);

	//press any key to exit
	system("PAUSE");

	//stop the pipe server
	pPipeServer->Stop();

	//wait for the thread to finish
	if (t1.joinable())
		t1.join();

	delete pPipeServer;
}
```

### 客户端使用
```C++
VOID OnMessage(MemBuffer* InBuf)
{
	printf("message from server: %s\n", (char*)InBuf->AccessMem());
}

int main()
{
	CNamedPipeClient* pPipeClient = new CNamedPipeClient();

	//connect to the pipe server
	DWORD dwErr = pPipeClient->Connect(TEXT("\\\\.\\pipe\\testpipe"), OnMessage);
	if (dwErr != 0)
	{
		printf("Failed to connect to the pipe server, error code: %d\n", dwErr);
                return -1;
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

        //wait server message, or press any key to exit
        system("PAUSE");

	//disconnect from the pipe server
	pPipeClient->Disconnect();

	delete pPipeClient;
}
```

