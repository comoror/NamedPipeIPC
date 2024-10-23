#pragma once

#include <windows.h>
#include <tchar.h>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>

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


#include "CNamedPipeClient.h"
#include "CNamedPipeServer.h"


