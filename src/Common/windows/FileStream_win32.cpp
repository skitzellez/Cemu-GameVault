#include "Common/windows/FileStream_win32.h"

namespace
{
HANDLE CemuCreateFile(const wchar_t* path, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition)
{
#if defined(CEMU_UWP)
	// Windows refuses paths past MAX_PATH unless they use the extended-length form, and some game saves go past it
	// (Mario Kart 8's highlight reels reach about 300 characters under LocalState)
	std::wstring extendedPath;
	const size_t length = wcslen(path);
	if (length >= MAX_PATH && path[1] == L':')
	{
		extendedPath.reserve(length + 4);
		extendedPath = L"\\\\?\\";
		extendedPath.append(path, length);
		std::replace(extendedPath.begin() + 4, extendedPath.end(), L'/', L'\\');
		path = extendedPath.c_str();
	}
	CREATEFILE2_EXTENDED_PARAMETERS params{};
	params.dwSize = sizeof(params);
	return CreateFile2(path, desiredAccess, shareMode, creationDisposition, &params);
#else
	return CreateFileW(path, desiredAccess, shareMode, NULL, creationDisposition, 0, 0);
#endif
}
}

namespace
{
std::mutex s_netFilesLock;
std::map<std::wstring, std::shared_ptr<FileStreamNetSource>> s_netFiles;

std::wstring NetFileKey(std::wstring path)
{
	for (auto& c : path)
		c = c == L'/' ? L'\\' : towlower(c);
	return path;
}
}

void FileStream::RegisterNetFile(const fs::path& standIn, std::shared_ptr<FileStreamNetSource> source)
{
	std::scoped_lock lock(s_netFilesLock);
	s_netFiles[NetFileKey(standIn.wstring())] = std::move(source);
}

bool FileStream::IsNetFile(const fs::path& path)
{
	std::scoped_lock lock(s_netFilesLock);
	return !s_netFiles.empty() && s_netFiles.count(NetFileKey(path.wstring())) != 0;
}

FileStream* FileStream::openNet(const std::wstring& path)
{
	std::shared_ptr<FileStreamNetSource> source;
	{
		std::scoped_lock lock(s_netFilesLock);
		if (s_netFiles.empty())
			return nullptr;
		auto it = s_netFiles.find(NetFileKey(path));
		if (it == s_netFiles.end())
			return nullptr;
		source = it->second;
	}
	return new FileStream(std::move(source));
}

FileStream* FileStream::openFile(std::string_view path)
{
	if (FileStream* net = openNet(boost::nowide::widen(path.data(), path.size())))
		return net;
	HANDLE hFile = CemuCreateFile(boost::nowide::widen(path.data(), path.size()).c_str(), FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;
	return new FileStream(hFile);
}

FileStream* FileStream::openFile(const wchar_t* path, bool allowWrite)
{
	if (!allowWrite)
		if (FileStream* net = openNet(path))
			return net;
	HANDLE hFile = CemuCreateFile(path, allowWrite ? (FILE_GENERIC_READ | FILE_GENERIC_WRITE) : FILE_GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;
	return new FileStream(hFile);
}

FileStream* FileStream::openFile2(const fs::path& path, bool allowWrite)
{
	return openFile(path.generic_wstring().c_str(), allowWrite);
}

FileStream* FileStream::createFile(const wchar_t* path)
{
	HANDLE hFile = CemuCreateFile(path, FILE_GENERIC_READ | FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;
	return new FileStream(hFile);
}

FileStream* FileStream::createFile(std::string_view path)
{
	auto w = boost::nowide::widen(path.data(), path.size());
	HANDLE hFile = CemuCreateFile(w.c_str(), FILE_GENERIC_READ | FILE_GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;
	return new FileStream(hFile);
}

FileStream* FileStream::createFile2(const fs::path& path)
{
	return createFile(path.generic_wstring().c_str());
}

std::optional<std::vector<uint8>> FileStream::LoadIntoMemory(const fs::path& path)
{
	FileStream* fs = openFile2(path);
	if (!fs)
		return std::nullopt;
	uint64 fileSize = fs->GetSize();
	if(fileSize > 0xFFFFFFFFull)
	{
		delete fs;
		return std::nullopt;
	}
	std::optional<std::vector<uint8>> v(fileSize);
	if (fs->readData(v->data(), (uint32)fileSize) != (uint32)fileSize)
	{
		delete fs;
		return std::nullopt;
	}
	delete fs;
	return v;
}

void FileStream::SetPosition(uint64 pos)
{
	if (m_net)
	{
		m_netPos = pos;
		return;
	}
	LARGE_INTEGER distance{};
	distance.QuadPart = pos;
	SetFilePointerEx(m_hFile, distance, nullptr, FILE_BEGIN);
}

uint64 FileStream::GetSize()
{
	if (m_net)
		return m_net->Size();
	LARGE_INTEGER fileSize{};
	GetFileSizeEx(m_hFile, &fileSize);
	return (uint64)fileSize.QuadPart;
}

bool FileStream::SetEndOfFile()
{
	if (m_net)
		return false;
	return ::SetEndOfFile(m_hFile) != 0;
}

void FileStream::extract(std::vector<uint8>& data)
{
	DWORD fileSize = (DWORD)std::min<uint64>(GetSize(), std::numeric_limits<DWORD>::max());
	data.resize(fileSize);
	SetPosition(0);
	if (m_net)
	{
		data.resize(readData(data.data(), fileSize));
		return;
	}
	DWORD bt;
	ReadFile(m_hFile, data.data(), fileSize, &bt, nullptr);
}

uint32 FileStream::readData(void* data, uint32 length)
{
	if (m_net)
	{
		uint64 size = m_net->Size();
		uint32 n = m_netPos >= size ? 0 : (uint32)std::min<uint64>(length, size - m_netPos);
		if (n && !m_net->Read(m_netPos, data, n))
			return 0;
		m_netPos += n;
		return n;
	}
	DWORD bytesRead = 0;
	ReadFile(m_hFile, data, length, &bytesRead, NULL);
	return bytesRead;
}

bool FileStream::readU64(uint64& v)
{
	return readData(&v, sizeof(uint64)) == sizeof(uint64);
}

bool FileStream::readU32(uint32& v)
{
	return readData(&v, sizeof(uint32)) == sizeof(uint32);
}

bool FileStream::readU8(uint8& v)
{
	return readData(&v, sizeof(uint8)) == sizeof(uint8);
}

bool FileStream::readLine(std::string& line)
{
	line.clear();
	uint8 c;
	bool isEOF = true;
	while (readU8(c))
	{
		isEOF = false;
		if(c == '\r')
			continue;
		if (c == '\n')
			break;
		line.push_back((char)c);
	}
	return !isEOF;
}

sint32 FileStream::writeData(const void* data, sint32 length)
{
	if (m_net)
		return 0;   // read only
	DWORD bytesWritten = 0;
	WriteFile(m_hFile, data, length, &bytesWritten, NULL);
	return bytesWritten;
}

void FileStream::writeU64(uint64 v)
{
	writeData(&v, sizeof(uint64));
}

void FileStream::writeU32(uint32 v)
{
	writeData(&v, sizeof(uint32));
}

void FileStream::writeU8(uint8 v)
{
	writeData(&v, sizeof(uint8));
}

void FileStream::writeStringFmt(const char* format, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	writeData(buffer, (sint32)strlen(buffer));
}

void FileStream::writeString(const char* str)
{
	writeData(str, (sint32)strlen(str));
}

void FileStream::writeLine(const char* str)
{
	writeData(str, (sint32)strlen(str));
	writeData("\r\n", 2);
}

FileStream::~FileStream()
{
	if(m_isValid)
		CloseHandle(m_hFile);
}

FileStream::FileStream(HANDLE hFile)
{
	m_hFile = hFile;
	m_isValid = true;
}

FileStream::FileStream(std::shared_ptr<FileStreamNetSource> net) : m_net(std::move(net))
{
}

bool FileStream::WriteFileAtomic(const fs::path& path, std::span<uint8> fileData, bool allowTargetFileRename)
{
	std::error_code ec;
	fs::path altPath = path;
	altPath.replace_extension( _utf8ToPath(_pathToUtf8(altPath.extension()).append("_tmp")));
	FileStream* fsAlt = FileStream::createFile2(altPath);
	if (!fsAlt)
		return false;
	if (fsAlt->writeData(fileData.data(), fileData.size()) != fileData.size())
	{
		delete fsAlt;
		fs::remove(altPath, ec);
		return false;
	}
	// only on unix: fsAlt->Flush();
	delete fsAlt;
	fs::rename(altPath, path, ec);
	if (ec)
	{
		// the target file might be in use, try to rename it if allowed
		// this isn't strictly atomic
		if (allowTargetFileRename)
		{
			fs::path backupPath = path;
			backupPath.replace_extension( _utf8ToPath(_pathToUtf8(backupPath.extension()).append(".backup")));
			std::error_code ec;
			fs::rename(path, backupPath, ec);
			if (ec)
			{
				cemuLog_log(LogType::Force, "Failed to atomically replace file {}, error {}", _pathToUtf8(path), ec.message());
				fs::remove(altPath, ec);
				return false;
			}
			// target file renamed, try to replace it again
			fs::rename(altPath, path, ec);
			if (ec)
			{
				cemuLog_log(LogType::Force, "Failed to atomically replace file {}, error {}", _pathToUtf8(path), ec.message());
				fs::rename(backupPath, path, ec); // try to undo rename
				if (ec)
					cemuLog_log(LogType::Force, "Failed to restore original file {}, error {}", _pathToUtf8(path), ec.message());
				fs::remove(altPath, ec);
				return false;
			}
		}
		else if (ec)
		{
			cemuLog_log(LogType::Force, "Failed to atomically replace file {}, error {}", _pathToUtf8(path), ec.message());
			fs::remove(altPath, ec);
			return false;
		}
	}
	return true;
}
