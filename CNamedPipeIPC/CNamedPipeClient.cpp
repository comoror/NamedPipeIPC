#include "pch.h"
#include "CNamedPipeClient.h"

CNamedPipeClient::CNamedPipeClient() : m_InBuffer(std::make_unique<MemBuffer>())
{
	m_hEventExit = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hEventRead = CreateEvent(NULL, TRUE/*Manual-Reset*/, FALSE, NULL);

	m_ovRead.hEvent = m_hEventRead;
}

CNamedPipeClient::~CNamedPipeClient()
{
	if (m_hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
	}

	if (m_hEventExit != NULL)
	{
		CloseHandle(m_hEventExit);
		m_hEventExit = NULL;
	}

	if (m_hEventRead != NULL)
	{
		CloseHandle(m_hEventRead);
		m_hEventRead = NULL;
	}
}

DWORD CNamedPipeClient::Connect(LPCTSTR lpszPipeName, PPIPE_ON_MESSAGE pOnMessage,
	PPIPE_ON_CONNECT pOnConnect,
	PPIPE_ON_DISCONNECT pOnDisconnect)
{
	DWORD dwErr = 0;

	m_pOnMessage = pOnMessage;
	m_pOnConnect = pOnConnect;
	m_pOnDisconnect = pOnDisconnect;

	if (m_hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
	}

	// Try to open a named pipe using overlapped I/O, wait if necessary.
	while (1)
	{
		m_hPipe = CreateFile(
			lpszPipeName,   // pipe name 
			GENERIC_READ |  // read and write access 
			GENERIC_WRITE,
			0,              // no sharing 
			NULL,           // default security attributes
			OPEN_EXISTING,  // opens existing pipe 
			FILE_FLAG_OVERLAPPED,              // default attributes 
			NULL);          // no template file

		// Break if the pipe handle is valid. 

		if (m_hPipe != INVALID_HANDLE_VALUE)
			break;

		// Exit if an error other than ERROR_PIPE_BUSY occurs. 
		dwErr = GetLastError();
		if (dwErr != ERROR_PIPE_BUSY)
		{
			DBG_PRINT("Could not open pipe. Error=%d\n", dwErr);
			return dwErr;
		}

		// All pipe instances are busy, so wait for 20 seconds. 

		if (!WaitNamedPipe(lpszPipeName, 20000))
		{
			DBG_PRINT("Could not open pipe: 20 second wait timed out.");
			return -1;
		}
	}

	// The pipe connected; change to message-read mode. 
	// For a pipe client, a pipe handle returned by CreateFile is always in byte-read mode initially
	// and must be changed to message-read mode before messages can be read from the pipe.
	// The pipe handle must have the FILE_WRITE_ATTRIBUTES access right (GENERIC_WRITE)
	DWORD dwMode = PIPE_READMODE_MESSAGE;
	BOOL fSuccess = SetNamedPipeHandleState(
		m_hPipe,    // pipe handle 
		&dwMode,  // new pipe mode 
		NULL,     // don't set maximum bytes 
		NULL);    // don't set maximum time 
	if (!fSuccess)
	{
		dwErr = GetLastError();
		DBG_PRINT("SetNamedPipeHandleState failed. GLE=%d\n", dwErr);

		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
		return dwErr;
	}

	if (m_pOnConnect)
	{
		m_pOnConnect();
	}

	// Create a thread for this client.
	m_thread = std::thread([=]() {

		HANDLE hEvents[2] = { m_hEventExit, m_ovRead.hEvent };

		while (1)
		{
			ReadPipe();

			DWORD dwRes = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
			if (dwRes == WAIT_OBJECT_0)
			{
				// Exit the thread
				break;
			}

			if (dwRes == WAIT_OBJECT_0 + 1)
			{
				if (m_bPendingIO)
				{
					//get overlapped result for read
					DWORD numBytesTransferred;
					BOOL success = GetOverlappedResult(m_hPipe, &m_ovRead, &numBytesTransferred, FALSE);
					if (!success && numBytesTransferred == 0)
					{
						//error happened
						DBG_PRINT("GetOverlappedResult failed 1. GLE=%d\n", GetLastError());
						break;
					}
					else if (!success && numBytesTransferred > 0)
					{
						DBG_PRINT("GetOverlappedResult failed 2. GLE=%d\n", GetLastError());
						break;
					}
					else
					{
						//completed
						DBG_PRINT("Read overlapped successfully completed: %s\n", m_PipeReadBuffer);
						m_InBuffer->AddItem(m_PipeReadBuffer, numBytesTransferred);
						OnMessage();
					}
				}
				else
				{
					//read completed
					DBG_PRINT("Read successfully completed: %s\n", m_PipeReadBuffer);
					m_InBuffer->AddItem(m_PipeReadBuffer, MAX_PIPE_BUFFER_SIZE);
					OnMessage();
				}
			}
		}

		if (m_pOnDisconnect)
		{
			m_pOnDisconnect();
		}

		});

	return 0;
}

DWORD CNamedPipeClient::Disconnect()
{
	if (m_hEventExit != NULL)
	{
		SetEvent(m_hEventExit);
	}

	if (m_thread.joinable())
	{
		m_thread.join();
	}

	return 0;
}

DWORD CNamedPipeClient::SendMessage(LPVOID msg, DWORD msg_size)
{
	//lock
	std::lock_guard<std::mutex> lock(m_mutexWrite);

	if (msg == NULL || msg_size == 0)
	{
		return -1;
	}

	DWORD dwWritten;
	OVERLAPPED ovWrite = { 0 };

	BOOL bWrite = WriteFile(m_hPipe, msg, msg_size, &dwWritten, &ovWrite);
	if (bWrite)
	{
		//write completed
		DBG_PRINT("Write successfully completed\n");
	}
	else
	{
		DWORD dwErr = GetLastError();
		if (dwErr == ERROR_IO_PENDING)
		{
			DBG_PRINT("WriteFile to pipe pending \n");

			BOOL bWaitResult = GetOverlappedResult(m_hPipe, &ovWrite, &dwWritten, TRUE);
			if (bWaitResult)
			{
				//write completed
				DBG_PRINT("Write overlapped successfully completed\n");
			}
			else
			{
				//error
				DBG_PRINT("WriteFile to pipe failed. GLE=%d\n", GetLastError());
				Disconnect();

				return dwErr;
			}
		}
		else
		{
			//error
			DBG_PRINT("WriteFile to pipe failed. GLE=%d\n", GetLastError());
			Disconnect();

			return dwErr;
		}
	}

	return 0;
}

DWORD CNamedPipeClient::ReadPipe()
{
	memset(m_PipeReadBuffer, 0, MAX_PIPE_BUFFER_SIZE);
	m_InBuffer->ClearMemory();

	DWORD dwRead;
	BOOL bRead = ReadFile(m_hPipe, m_PipeReadBuffer, MAX_PIPE_BUFFER_SIZE, &dwRead, &m_ovRead);
	if (bRead)
	{
		//read completed
		m_bPendingIO = FALSE;
	}
	else
	{
		DWORD dwErr = GetLastError();
		if (dwErr == ERROR_IO_PENDING)
		{
			//pending IO
			DBG_PRINT("ReadFile from pipe pending \n");
			m_bPendingIO = TRUE;
		}
		else
		{
			//error
			DBG_PRINT("ReadFile from pipe failed. GLE=%d\n", GetLastError());
			m_bPendingIO = FALSE;
			return dwErr;
		}
	}

	return 0;
}

VOID CNamedPipeClient::OnMessage()
{
	if (m_pOnMessage)
	{
		m_pOnMessage(m_InBuffer.get());
	}
}
