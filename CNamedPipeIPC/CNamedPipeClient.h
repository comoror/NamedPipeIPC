#pragma once

#include <windows.h>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>

#define MAX_PIPE_BUFFER_SIZE	4096

class MemBuffer {
public:
	MemBuffer() {}
	size_t GetCurrentSize() { return buffer_.size(); }
	bool AddItem(LPVOID item, size_t item_size) {
		if (item == nullptr) {
			return false;
		}
		BYTE* copy_ptr = (BYTE*)item;
		for (size_t i = 0; i < item_size; i++) {
			try {
				buffer_.emplace_back(*copy_ptr++);
			}
			catch (...) {
				return false;
			}
		}
		return true;
	}
	LPVOID AccessMem() { return buffer_.data(); }
	void ClearMemory() { buffer_.clear(); }

private:
	std::vector<BYTE> buffer_;
};

typedef VOID(*PPIPE_ON_CONNECT) ();
typedef VOID(*PPIPE_ON_DISCONNECT) ();
typedef VOID(*PPIPE_ON_MESSAGE) (MemBuffer* inBuf);

class CNamedPipeClient
{
public:
	CNamedPipeClient();
	~CNamedPipeClient();

	DWORD Connect(LPCTSTR lpszPipeName, PPIPE_ON_MESSAGE pOnMessage, 
		PPIPE_ON_CONNECT pOnConnect = NULL, 
		PPIPE_ON_DISCONNECT pOnDisconnect = NULL);
	DWORD Disconnect();

	DWORD SendMessage(LPVOID msg, DWORD msg_size);

private:
	DWORD ReadPipe();

	VOID OnMessage();

	BYTE						m_PipeReadBuffer[MAX_PIPE_BUFFER_SIZE] = { 0 };
	std::unique_ptr<MemBuffer>	m_InBuffer;

private:
	HANDLE				m_hPipe = INVALID_HANDLE_VALUE;
	HANDLE				m_hEventExit = NULL;
	HANDLE				m_hEventRead = NULL;

	OVERLAPPED			m_ovRead = { 0 };
	BOOL				m_bPendingIO = FALSE;

	std::thread			m_thread;
	std::mutex			m_mutexWrite;

	PPIPE_ON_MESSAGE	m_pOnMessage = NULL;
	PPIPE_ON_CONNECT	m_pOnConnect = NULL;
	PPIPE_ON_DISCONNECT	m_pOnDisconnect = NULL;
};
