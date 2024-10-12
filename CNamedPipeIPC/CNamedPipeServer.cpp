#include "pch.h"
#include "CNamedPipeServer.h"

CNamedPipeServer::CNamedPipeServer(LPCTSTR lpszPipeName, PON_MESSAGE_CALLBACK pOnMessage,
	PON_CONNECTED_CALLBACK pOnConnected,
	PON_DISCONNECTED_CALLBACK pOnDisconnected)
{
	m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	_tcscpy_s(m_szPipeName, lpszPipeName);
	m_pOnMessage = pOnMessage;
	m_pOnConnected = pOnConnected;
	m_pOnDisconnected = pOnDisconnected;

	//init the event value
	for (int i = 0; i < nMaxPipes; i++)
	{
		m_hEvents[i] = NULL;
	}

	m_hEvents[nMaxPipes] = m_hExitEvent;
}

CNamedPipeServer::~CNamedPipeServer()
{
	for (int i = 0; i < nMaxPipes; i++)
	{
		if (m_hEvents[i] != NULL)
		{
			CloseHandle(m_hEvents[i]);
			m_hEvents[i] = NULL;
		}
	}
}

DWORD CNamedPipeServer::Run()
{
	DWORD dwErr = 0;

	dwErr = CreatePipeAndEvents();
	if (dwErr != 0)
	{
		return dwErr;
	}

	while (1)
	{
		DWORD dwWaitResult = WaitForMultipleObjects(nMaxEvents, m_hEvents, FALSE, INFINITE);

		DWORD i = dwWaitResult - WAIT_OBJECT_0;
		if (i == nMaxPipes) //exit event
		{
			break;
		}

		DBG_PRINT("WaitForMultipleObjects %d\n", i);

		GetPendingOperationResult(i);
		//ExecuteCurrentState(i);
	}

	return 0;
}

VOID CNamedPipeServer::Stop()
{
	SetEvent(m_hExitEvent);
}

DWORD CNamedPipeServer::CreatePipeAndEvents()
{
	for (int i = 0; i < nMaxPipes; i++)
	{
		m_hEvents[i] = CreateEvent(
			NULL,   // default security attribute 
			TRUE,   // Manual-reset event 
			FALSE,  // initial state = not signaled 
			NULL);  // unnamed event object 

		if (m_hEvents[i] == NULL)
		{
			return GetLastError();
		}

		m_instPipes[i].mOverlap.hEvent = m_hEvents[i];
		DWORD dwErr = m_instPipes[i].CreatePipeInstance(m_szPipeName);
		if (dwErr != 0)
		{
			return dwErr;
		}

		dwErr = ConnectToNewClient(m_instPipes[i].mPipeInstance.get(), &m_instPipes[i].mOverlap, m_instPipes[i].mPendingIO);
	}
	return 0;
}

DWORD CNamedPipeServer::ConnectToNewClient(HANDLE pipe, LPOVERLAPPED overlapped, BOOL& pendingIO)
{
	BOOL connected = ConnectNamedPipe(pipe, overlapped);

	pendingIO = FALSE;

	if (connected == TRUE) 
	{
		return GetLastError();
	}

	switch (GetLastError()) 
	{
	case ERROR_IO_PENDING:
		pendingIO = TRUE;
		break;
	case ERROR_PIPE_CONNECTED:
		if (SetEvent(overlapped->hEvent)) 
		{
			break;
		}
	default:
		return GetLastError();
	}

	return ERROR_SUCCESS;
}

void CNamedPipeServer::DisconnectAndReconnect(DWORD pipeIndex)
{
	OnDisconnected(pipeIndex);
	m_instPipes[pipeIndex].mCurrentState = PipeStates::CONNECTING;

	if (!DisconnectNamedPipe(m_instPipes[pipeIndex].mPipeInstance.get())) 
	{
		return;
	}

	DWORD connectionStatus = ConnectToNewClient(m_instPipes[pipeIndex].mPipeInstance.get(),
		&m_instPipes[pipeIndex].mOverlap,
		m_instPipes[pipeIndex].mPendingIO);

	if (connectionStatus == ERROR_SUCCESS) 
	{
		if (m_instPipes[pipeIndex].mPendingIO)
		{
			return;
		}
	}
}

void CNamedPipeServer::GetPendingOperationResult(DWORD pipeIndex)
{
	if (m_instPipes[pipeIndex].mPendingIO) 
	{
		DWORD numBytesTransferred;

		BOOL success = GetOverlappedResult(m_instPipes[pipeIndex].mPipeInstance.get(),  // handle to pipe
				&m_instPipes[pipeIndex].mOverlap,  // OVERLAPPED structure
				&numBytesTransferred,        // bytes transferred
				FALSE);                      // do not wait

		switch (m_instPipes[pipeIndex].mCurrentState) 
		{
		case PipeStates::CONNECTING:
			if (!success) 
			{
				return;
			}
			OnConnected(pipeIndex);
			ReadPipe(pipeIndex);
			break;
		case PipeStates::READING:
			if (!success) 
			{
				DisconnectAndReconnect(pipeIndex);
				return;
			}
			else 
			{
				DBG_PRINT("Read overlapped successfully completed: %d\n", pipeIndex);
				m_instPipes[pipeIndex].mBytesRead = numBytesTransferred;
				m_instPipes[pipeIndex].mRequestBuffer->ClearMemory();
				m_instPipes[pipeIndex].mRequestBuffer->AddItem(m_instPipes[pipeIndex].mPipeReadBuffer,
					m_instPipes[pipeIndex].mBytesRead);

				OnMessage(pipeIndex);
				ReadPipe(pipeIndex);
			}
			break;
		}
	}
	else
	{
		if (m_instPipes[pipeIndex].mCurrentState == PipeStates::CONNECTING)
		{
			OnConnected(pipeIndex);
			ReadPipe(pipeIndex);
		}
		else if (m_instPipes[pipeIndex].mCurrentState == PipeStates::READING)
		{
			m_instPipes[pipeIndex].mRequestBuffer->ClearMemory();
			m_instPipes[pipeIndex].mRequestBuffer->AddItem(m_instPipes[pipeIndex].mPipeReadBuffer,
				m_instPipes[pipeIndex].mBytesRead);
			OnMessage(pipeIndex);
			ReadPipe(pipeIndex);
		}
		else
		{
			//do nothing
		}
	}

	return;
}

//void CNamedPipeServer::ExecuteCurrentState(DWORD pipeIndex)
//{
//	switch (m_instPipes[pipeIndex].mCurrentState) 
//	{
//	case PipeStates::READING:
//		ReadPipe(pipeIndex);
//		break;
//	case PipeStates::WRITING:
//		OnMessage(pipeIndex);
//		WritePipe(pipeIndex);
//		break;
//	case PipeStates::CONNECTING:
//		break;
//	default:
//		break;
//	}
//
//	return;
//}

DWORD CNamedPipeServer::ReadPipe(DWORD pipeIndex)
{
	m_instPipes[pipeIndex].mCurrentState = PipeStates::READING;

	BOOL success = ReadFile(m_instPipes[pipeIndex].mPipeInstance.get(), 
		m_instPipes[pipeIndex].mPipeReadBuffer,
		nMaxBufferSize, 
		&m_instPipes[pipeIndex].mBytesRead, 
		&m_instPipes[pipeIndex].mOverlap);

	if (success && m_instPipes[pipeIndex].mBytesRead != 0) 
	{
		DBG_PRINT("Read successfully completed: %d\n", pipeIndex);
		m_instPipes[pipeIndex].mPendingIO = FALSE;
		return 0;
	}

	DWORD lastError = GetLastError();
	if (!success &&	((lastError == ERROR_IO_PENDING) || lastError == ERROR_MORE_DATA)) 
	{
		DBG_PRINT("ReadFile to pipe pending: %d\n", pipeIndex);
		m_instPipes[pipeIndex].mPendingIO = TRUE;
		return 0;
	}

	// An error occurred; disconnect from the client.
	DisconnectAndReconnect(pipeIndex);

	return lastError;
}

DWORD CNamedPipeServer::WritePipe(DWORD pipeIndex)
{
	//lock
	std::lock_guard<std::mutex> lock(m_mutexWrite);

	if (m_instPipes[pipeIndex].mResponseBuffer->GetCurrentSize() == 0)
	{
		return 0;
	}

	DWORD bytesWritten;
	DWORD expectedBytesWritten;
	BOOL success;

	OVERLAPPED ovWrite = { 0 };
	success = WriteFile(m_instPipes[pipeIndex].mPipeInstance.get(),
		m_instPipes[pipeIndex].mResponseBuffer->AccessMem(),
		static_cast<DWORD>(m_instPipes[pipeIndex].mResponseBuffer->GetCurrentSize()),
		&bytesWritten, 
		&ovWrite);

	DBG_PRINT("WriteFile to pipe %d: %s\n", pipeIndex, (char*)m_instPipes[pipeIndex].mResponseBuffer->AccessMem());

	expectedBytesWritten =	static_cast<DWORD>(m_instPipes[pipeIndex].mResponseBuffer->GetCurrentSize());

	if (success && bytesWritten == expectedBytesWritten) 
	{
		DBG_PRINT("Write successfully completed: %d\n", pipeIndex);
		m_instPipes[pipeIndex].mResponseBuffer->ClearMemory();
		return 0;
	}

	DWORD lastError = GetLastError();
	if (!success && (lastError == ERROR_IO_PENDING)) 
	{
		DBG_PRINT("WriteFile to pipe pending: %d\n", pipeIndex);
		BOOL bWaitResult = GetOverlappedResult(m_instPipes[pipeIndex].mPipeInstance.get(), &ovWrite, &bytesWritten, TRUE);
		if (bWaitResult)
		{
			//write completed
			DBG_PRINT("Write overlapped successfully completed: %d\n", pipeIndex);
			m_instPipes[pipeIndex].mResponseBuffer->ClearMemory();
			return 0;
		}
		else
		{
			//error
			lastError = GetLastError();
			DBG_PRINT("WriteFile to pipe failed. GLE=%d: %d\n", lastError, pipeIndex);

			// An error occurred; disconnect from the client.
			DisconnectAndReconnect(pipeIndex);
			return lastError;
		}
	}

	return 0;
}

DWORD CNamedPipeServer::SendMessage(DWORD pipeIndex, LPVOID msg, size_t msg_size)
{
	m_instPipes[pipeIndex].mResponseBuffer->AddItem(msg, msg_size);
	return WritePipe(pipeIndex);
}

VOID CNamedPipeServer::BroadcastMessage(LPVOID msg, size_t msg_size)
{
	for (int i = 0; i < nMaxPipes; i++)
	{
		if (m_instPipes[i].mCurrentState != PipeStates::CONNECTING)
		{
			SendMessage(i, msg, msg_size);
		}
	}
}

void CNamedPipeServer::OnMessage(DWORD pipeIndex)
{
	if (m_pOnMessage != NULL)
	{
		m_pOnMessage(pipeIndex, m_instPipes[pipeIndex].mRequestBuffer.get(), m_instPipes[pipeIndex].mResponseBuffer.get());

		WritePipe(pipeIndex);
	}
}

void CNamedPipeServer::OnConnected(DWORD pipeIndex)
{
	if (m_pOnConnected != NULL)
	{
		m_pOnConnected(pipeIndex);
	}
}

void CNamedPipeServer::OnDisconnected(DWORD pipeIndex)
{
	if (m_pOnDisconnected != NULL)
	{
		m_pOnDisconnected(pipeIndex);
	}
}
