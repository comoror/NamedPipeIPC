#pragma once

#include "CNamedPipeIPC.h"

#define MAX_PIPE_INSTANCES		16
#define MAX_PIPE_BUFFER_SIZE	4096

typedef VOID (*PON_CONNECTED_CALLBACK) (DWORD pipeIndex);
typedef VOID (*PON_DISCONNECTED_CALLBACK) (DWORD pipeIndex);
typedef VOID (*PON_MESSAGE_CALLBACK) (DWORD pipeIndex, MemBuffer* rqstBuf, MemBuffer* rspBuf);

class CNamedPipeServer
{
public:
	CNamedPipeServer(LPCTSTR lpszPipeName, PON_MESSAGE_CALLBACK pOnMessage,
		PON_CONNECTED_CALLBACK pOnConnected = NULL,
		PON_DISCONNECTED_CALLBACK pOnDisconnected = NULL);
	~CNamedPipeServer();

	//delete copy constructor and assignment operator
	CNamedPipeServer(const CNamedPipeServer&) = delete;
	CNamedPipeServer& operator=(const CNamedPipeServer&) = delete;

	DWORD Run();
	VOID Stop();

	DWORD SendMessage(DWORD pipeIndex, LPVOID msg, size_t msg_size);
	VOID BroadcastMessage(LPVOID msg, size_t msg_size);

private:
	DWORD CreatePipeAndEvents();
	DWORD ConnectToNewClient(HANDLE pipe, LPOVERLAPPED overlapped,	BOOL& pendingIO);
	void DisconnectAndReconnect(DWORD pipeIndex);

	void GetPendingOperationResult(DWORD pipeIndex);
	//void ExecuteCurrentState(DWORD pipeIndex);

	DWORD ReadPipe(DWORD pipeIndex);
	DWORD WritePipe(DWORD pipeIndex);

	void OnMessage(DWORD pipeIndex);
	void OnConnected(DWORD pipeIndex);
	void OnDisconnected(DWORD pipeIndex);

private:
	static const int nMaxPipes = MAX_PIPE_INSTANCES;
	static const int nMaxEvents = nMaxPipes + 1;

	static const int nMaxBufferSize = MAX_PIPE_BUFFER_SIZE;
	static const int nPipeTimeout = 5000;

	enum class PipeStates { CONNECTING, READING };

	struct PipeInstanceDeleter 
	{
		void operator()(HANDLE pipeInstance) const 
		{
			if (pipeInstance != INVALID_HANDLE_VALUE) 
			{
				FlushFileBuffers(pipeInstance);
				DisconnectNamedPipe(pipeInstance);
				CloseHandle(pipeInstance);
			}
		}
	};

	class Pipe
	{
	public:
		Pipe() : mRequestBuffer(std::make_unique<MemBuffer>()),	mResponseBuffer(std::make_unique<MemBuffer>()) {}

		//delete copy constructor and assignment operator
		Pipe(const Pipe&) = delete;
		Pipe& operator=(const Pipe&) = delete;

		DWORD CreatePipeInstance(LPCTSTR lpszPipeName)
		{
			HANDLE hPipe = CreateNamedPipe(
				lpszPipeName,
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
				nMaxPipes,
				nMaxBufferSize,
				nMaxBufferSize,
				nPipeTimeout,
				NULL);
			if (hPipe == INVALID_HANDLE_VALUE)
			{
				return GetLastError();
			}

			mPipeInstance.reset(hPipe);

			return ERROR_SUCCESS;
		}

		std::unique_ptr<MemBuffer> mRequestBuffer;
		std::unique_ptr<MemBuffer> mResponseBuffer;

		std::unique_ptr<std::remove_pointer_t<HANDLE>, PipeInstanceDeleter>	mPipeInstance;

		OVERLAPPED	mOverlap = {};
		BOOL		mPendingIO = FALSE;
		PipeStates	mCurrentState = PipeStates::CONNECTING;
	
		BYTE		mPipeReadBuffer[nMaxBufferSize] = { 0 };
		DWORD		mBytesRead = 0;
	};

	TCHAR	m_szPipeName[MAX_PATH] = { 0 };

	Pipe	m_instPipes[nMaxPipes];
	HANDLE  m_hExitEvent = NULL;
	HANDLE	m_hEvents[nMaxEvents];

	std::mutex					m_mutexWrite;

	PON_CONNECTED_CALLBACK		m_pOnConnected = NULL;
	PON_DISCONNECTED_CALLBACK	m_pOnDisconnected = NULL;
	PON_MESSAGE_CALLBACK		m_pOnMessage = NULL;
};

