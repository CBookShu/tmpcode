#pragma once
#include <mutex>
#include <thread>
#include <unordered_map>
#include <set>
#include <functional>

#include <Windows.h>

/*
监听本地文件是否被修改
每个文件监听，一个定时器

源代思路是从libuv中文件监听抠出来的，做了简单的封装，方便使用
win32使用iocp监听文件修改的方式，只能监听某个文件夹，至少暂时未发现其他更好的接口
此外文件修改一次，会连续触发很多次，所以不得不做了一个简单的延迟，把同一时间多次的
触发合并为一次，这里用的是2s
*/

struct FileListener {
	std::string filepath;
	std::function<void(const std::string&)>callback;
};

struct DirListener {
	HANDLE m_hFile;
	char buffer[4096];
};

class FileWatcher
{
public:
	enum {
		CMD_ADD,
		CMD_READD,
		CMD_CANCEL,
		CMD_OP,
		CMD_STOP,
	};
	template <typename T>
	struct OPTData {
		OVERLAPPED overlapped;
		T t;
	};

	using DirListenData = OPTData<DirListener>;
	using FileListenerData = OPTData<FileListener>;

	bool Init(const char* dir);
	void Unint();
	void OnTest(bool& ok, std::string& cmd);

	std::ptrdiff_t addpathlistener(const std::string& path, std::function<void(const std::string&)>callback);
	void erase(std::ptrdiff_t p);
	template <typename T>
	T* allocOPType() {
		auto* pr = new OPTData<T>;
		return &pr->t;
	}
	template <typename T>
	void freeOPType(T*t) {
		auto* r = CONTAINING_RECORD(t, OPTData<T>, t);
		delete r;
	}
	template <typename T>
	OPTData<T>* getOPData(T* t) {
		auto* r = CONTAINING_RECORD(t, OPTData<T>, t);
		return r;
	}

protected:
	void ReAdd(DirListenData* node);
	void OnThreadFunc();

	void OnReAddListener(DirListenData* node);
	void OnOpFileModify(DirListenData* node, std::set<std::string>& delayset);
private:
	DirListenData m_dirinfo;
	std::thread m_th;
	HANDLE m_hIOCP = INVALID_HANDLE_VALUE;
};