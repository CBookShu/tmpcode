#include "filewatcher.h"

#define LOG_ERROR(fmt,...)  
#define LOG_INFO(fmt,...)

#define SAFE_CLOSE_WINHANDLE(h)	if(h != INVALID_HANDLE_VALUE) {CloseHandle(h);h = INVALID_HANDLE_VALUE;}

static int my_getlasterror() {
	int v = GetLastError();
	return v;
}

bool FileWatcher::Init(const char* dir)
{
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
	if (m_hIOCP == INVALID_HANDLE_VALUE) {
		LOG_ERROR("CreateIoCompletionPort ERROR %d", my_getlasterror());
		return false;
	}
	HANDLE h = CreateFileA(dir,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_DELETE |
		FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
		FILE_FLAG_OVERLAPPED,
		NULL);
	if (h == INVALID_HANDLE_VALUE) {
		auto e = my_getlasterror();
		LOG_ERROR("CreateFile %s err %d", dir.c_str(), e);
		SAFE_CLOSE_WINHANDLE(m_hIOCP);
		return false;
	}
	m_dirinfo.t.m_hFile = h;
	if (!CreateIoCompletionPort(m_dirinfo.t.m_hFile, m_hIOCP, CMD_OP, 0)) {
		auto v = my_getlasterror();
		LOG_ERROR("CreateIoCompletionPort %s err %d", dir.c_str(), v);
		SAFE_CLOSE_WINHANDLE(m_dirinfo.t.m_hFile);
		SAFE_CLOSE_WINHANDLE(m_hIOCP);
		return false;
	}

	ZeroMemory(&(m_dirinfo.overlapped), sizeof(m_dirinfo.overlapped));
	if (!ReadDirectoryChangesW(m_dirinfo.t.m_hFile,
		m_dirinfo.t.buffer,
		sizeof(m_dirinfo.t.buffer),
		FALSE,
		FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_DIR_NAME |
		FILE_NOTIFY_CHANGE_ATTRIBUTES |
		FILE_NOTIFY_CHANGE_SIZE |
		FILE_NOTIFY_CHANGE_LAST_WRITE |
		FILE_NOTIFY_CHANGE_LAST_ACCESS |
		FILE_NOTIFY_CHANGE_CREATION |
		FILE_NOTIFY_CHANGE_SECURITY,
		NULL,
		&(m_dirinfo.overlapped),
		NULL)) {
		auto v = my_getlasterror();
		LOG_ERROR("ReadDirectoryChangesW %s err %d", dir.c_str(), v);
		SAFE_CLOSE_WINHANDLE(m_dirinfo.t.m_hFile);
		SAFE_CLOSE_WINHANDLE(m_hIOCP);
		return false;
	}

	m_th = std::thread([this](){
		OnThreadFunc();
	});

    return true;
}

void FileWatcher::Unint()
{
	if (m_hIOCP == INVALID_HANDLE_VALUE) {
		return;
	}
	PostQueuedCompletionStatus(m_hIOCP, 0, CMD_STOP, &m_dirinfo.overlapped);
	m_th.join();
	SAFE_CLOSE_WINHANDLE(m_hIOCP);
	SAFE_CLOSE_WINHANDLE(m_dirinfo.t.m_hFile);
}

std::ptrdiff_t listener = 0;
void FileWatcher::OnTest(bool& ok, std::string& cmd)
{
}

std::ptrdiff_t FileWatcher::addpathlistener(const std::string& path, std::function<void(const std::string&)>callback)
{
	auto* f = allocOPType<FileListener>();
	auto* r = getOPData(f);
	f->callback = callback;
	f->filepath = path;
	if (!PostQueuedCompletionStatus(m_hIOCP, 0, CMD_ADD, &r->overlapped)) {
		auto e = GetLastError();
		LOG_ERROR("PostQueuedCompletionStatus Error %s %d", path.c_str(), e);
		delete r;
		return 0;
	}
	return (std::ptrdiff_t)f;
}

void FileWatcher::erase(std::ptrdiff_t l)
{
	if (l == 0) {
		return;
	}
	auto* f = (FileListener*)l;
	auto *r = getOPData(f);
	if (!PostQueuedCompletionStatus(m_hIOCP, 0, CMD_CANCEL, &r->overlapped)) {
		// 几乎是不可能的
		auto e = my_getlasterror();
		LOG_ERROR("PostQueuedCompletionStatus Error %d", e);
	}
}

void FileWatcher::ReAdd(DirListenData* f)
{
	if (!PostQueuedCompletionStatus(m_hIOCP, 0, CMD_READD, &f->overlapped)) {
		auto e = my_getlasterror();
		LOG_ERROR("PostQueuedCompletionStatus Error %d", e);
	}
}

static std::string _convert_utf16_to_utf8(WCHAR* utf16, int utf16len) {
	/* Check how much space we need */
	int bufsize = WideCharToMultiByte(CP_UTF8,
		0,
		utf16,
		utf16len,
		NULL,
		0,
		NULL,
		NULL);
	if (bufsize == 0) {
		return std::string();
	}
	std::string s;
	s.resize(bufsize);
	/* Convert to UTF-8 */
	bufsize = WideCharToMultiByte(CP_UTF8,
		0,
		utf16,
		utf16len,
		(char*)s.data(),
		bufsize,
		NULL,
		NULL);
	if (bufsize == 0) {
		return std::string();
	}
	return s;
}

void FileWatcher::OnThreadFunc()
{
	bool running = true;
	std::unordered_multimap<std::string, FileListener*> listeners;
	std::set<std::string> delayset;
	std::chrono::system_clock::time_point pre = std::chrono::system_clock::now();
	int delaytime = 2000;
	LOG_INFO("File listener begin");
	while (running) {
		OVERLAPPED_ENTRY overlappeds[128];
		ULONG count;
		ULONG_PTR key;

		if (delayset.size()) {
			// 遇到一种情况，如果本地文件修改太频繁
			// 原来的优先级是有修改触发，就不再检查该队列，导致了修改的回调永远无法调用
			// 于是就只得把检查队列放在了这里
			auto now = std::chrono::system_clock::now();
			auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - pre).count();
			if (diff >= delaytime) {
				for (auto& path : delayset)
				{
					auto it = listeners.equal_range(path);
					for (auto b = it.first; b != it.second; ++b)
					{
						LOG_INFO("file modify %s", path.c_str());
						b->second->callback(path);
					}
				}
				delayset.clear();
			}
		}

		auto success = GetQueuedCompletionStatusEx(m_hIOCP,
			overlappeds,
			_countof(overlappeds),
			&count,
			// 这里增加延时，是因为我们平常的文件修改，这里会触发很多次
			// 为了业务使用方便，延时一下，把一定时间内同文件的触发合并一下
			delayset.size() ? delaytime : INFINITE,
			FALSE);
		if (!success) {
			auto e = my_getlasterror();
			if (e != WAIT_TIMEOUT) {
				LOG_ERROR("GetQueuedCompletionStatusEx %d", e);
			}
			continue;
		}

		for (int i = 0; i < count; i++) {
			if (overlappeds[i].lpOverlapped) {
				auto cmd = overlappeds[i].lpCompletionKey;
				if (cmd == CMD_ADD) {
					FileListenerData* f = CONTAINING_RECORD(overlappeds[i].lpOverlapped, FileListenerData, overlapped);
					listeners.insert(std::make_pair(f->t.filepath, &f->t));
					LOG_INFO("file listener add %s", f->t.filepath.c_str());
				}
				else if (cmd == CMD_READD) {
					DirListenData* f = CONTAINING_RECORD(overlappeds[i].lpOverlapped, DirListenData, overlapped);
					OnReAddListener(f);
				}
				else if (cmd == CMD_CANCEL) {
					FileListenerData* f = CONTAINING_RECORD(overlappeds[i].lpOverlapped, FileListenerData, overlapped);
					LOG_INFO("file listener erase %s", f->t.filepath.c_str());
					auto it = listeners.equal_range(f->t.filepath);
					for (auto b = it.first; b != it.second; ++b)
					{
						if (b->second == &f->t) {
							listeners.erase(b);
							break;
						}
					}
					delete f;
				}
				else if (cmd == CMD_OP) {
					DirListenData* f = CONTAINING_RECORD(overlappeds[i].lpOverlapped, DirListenData, overlapped);
					bool c = delayset.empty();
					OnOpFileModify(f, delayset);
					if (!c && !delayset.empty()) {
						// 有新的文件了
						pre = std::chrono::system_clock::now();
					}
				}
				else if (cmd == CMD_STOP) {
					// 要结束了
					// TODO: 是否把 listeners 清理一下呢？还是让外部调用的接口自己清理？
					for (auto& it : listeners)
					{
						auto *f = (it.second);
						auto *r = getOPData(f);
						delete r;
					}
					listeners.clear();
					running = false;
				}
			}
		}

	}

	LOG_INFO("File listener end");
}

void FileWatcher::OnReAddListener(DirListenData* f)
{
	ZeroMemory(&(f->overlapped), sizeof(f->overlapped));
	if (!ReadDirectoryChangesW(f->t.m_hFile,
		f->t.buffer,
		sizeof(f->t.buffer),
		TRUE,
		FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_DIR_NAME |
		FILE_NOTIFY_CHANGE_ATTRIBUTES |
		FILE_NOTIFY_CHANGE_SIZE |
		FILE_NOTIFY_CHANGE_LAST_WRITE |
		FILE_NOTIFY_CHANGE_LAST_ACCESS |
		FILE_NOTIFY_CHANGE_CREATION |
		FILE_NOTIFY_CHANGE_SECURITY,
		NULL,
		&(f->overlapped),
		NULL)) {
		auto v = my_getlasterror();
		LOG_ERROR("ReadDirectoryChangesW  err %d", v);
		return;
	}
}

void FileWatcher::OnOpFileModify(DirListenData* node, std::set<std::string>& delayset)
{
	// 文件有调整
	int offset = 0;
	auto* file_info = (FILE_NOTIFY_INFORMATION*)(node->t.buffer + offset);
	if (node->overlapped.InternalHigh > 0) {
		do {
			file_info = (FILE_NOTIFY_INFORMATION*)((char*)file_info + offset);
			bool modify = false;
			bool rename = false;
			switch (file_info->Action)
			{
			case FILE_ACTION_ADDED:
			case FILE_ACTION_REMOVED:
			case FILE_ACTION_RENAMED_OLD_NAME:
			case FILE_ACTION_RENAMED_NEW_NAME:
				rename = true;
				break;
			case FILE_ACTION_MODIFIED:
				modify = true;
				break;
			default:
				break;
			}

			if (modify) {
				int size = file_info->FileNameLength / sizeof(WCHAR);
				std::string path = _convert_utf16_to_utf8(file_info->FileName, size);
				delayset.insert(path);
			}
			offset = file_info->NextEntryOffset;
		} while (offset);

		ReAdd(node);
	}
}
