#pragma once
#include "Common/precompiled.h"

// Game Vault: a file read from somewhere other than the disk (a game on a PC's shared folder or a NAS, see
// src/uwp/UwpNetGame.cpp), opened by FileStream under a stand-in path. Reads must be safe from several threads.
class FileStreamNetSource
{
 public:
	virtual ~FileStreamNetSource() = default;
	virtual uint64 Size() = 0;
	virtual bool Read(uint64 offset, void* buffer, size_t length) = 0;
};

class FileStream
{
 public:
	// Game Vault: openFile* of this path (any case, / or \) reads from the source instead of the disk
	static void RegisterNetFile(const fs::path& standIn, std::shared_ptr<FileStreamNetSource> source);
	static bool IsNetFile(const fs::path& path);

	static FileStream* openFile(std::string_view path);
	static FileStream* openFile(const wchar_t* path, bool allowWrite = false);
	static FileStream* openFile2(const fs::path& path, bool allowWrite = false);

	static FileStream* createFile(const wchar_t* path);
	static FileStream* createFile(std::string_view path);
	static FileStream* createFile2(const fs::path& path);

	// helper function to load a file into memory
	static std::optional<std::vector<uint8>> LoadIntoMemory(const fs::path& path);

	// size and seek
	void SetPosition(uint64 pos);

	uint64 GetSize();
	bool SetEndOfFile();
	void extract(std::vector<uint8>& data);

	// reading
	uint32 readData(void* data, uint32 length);
	bool readU64(uint64& v);
	bool readU32(uint32& v);
	bool readU16(uint16& v);
	bool readU8(uint8& v);
	bool readLine(std::string& line);

	// writing (binary)
	sint32 writeData(const void* data, sint32 length);
	void writeU64(uint64 v);
	void writeU32(uint32 v);
	void writeU16(uint16 v);
	void writeU8(uint8 v);

	// writing (strings)
	void writeStringFmt(const char* format, ...);
	void writeString(const char* str);
	void writeLine(const char* str);

	~FileStream();
	FileStream() = default;

	// atomic helper
	static bool WriteFileAtomic(const fs::path& path, std::span<uint8> fileData, bool allowTargetFileRename = false);

 private:
	FileStream(HANDLE hFile);
	FileStream(std::shared_ptr<FileStreamNetSource> net);
	static FileStream* openNet(const std::wstring& path);

	bool m_isValid{};
	HANDLE m_hFile{INVALID_HANDLE_VALUE};
	std::shared_ptr<FileStreamNetSource> m_net;   // set: a network file, read from here at m_netPos
	uint64 m_netPos{};
};
