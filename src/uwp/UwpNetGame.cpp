// Copyright (c) 2026 Skitzelles - part of Cemu for Game Vault (github.com/skitzellez/xbox-game-vault).
// Licensed like the rest of Cemu (MPL-2.0).
//
// Cemu for Game Vault: a Wii U disc image (.wud / .wux) played straight from a PC's shared folder or a NAS (Game
// Vault's network games), with no copy on the console first. Apps on the Xbox cannot open \\PC\share paths, so this
// speaks SMB2/3 itself through libsmb2 (smb2.dll, LGPL-2.1, the same build Game Vault uses, loaded only when a
// network game starts), as Xenia for Game Vault does for Xbox 360 games.
//
// Reads go through a cache of 1 MB pieces, so the game's many small reads do not cost a network round trip each;
// a dropped connection (a PC that slept, Wi-Fi that blinked) is opened again once by itself.

#include <winsock2.h>
#include <windows.h>

#include <fcntl.h>
#include <stdio.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <type_traits>
#include <vector>

#include "UwpNetGame.h"
#include "Common/FileStream.h"
#include "Cemu/Logging/CemuLogging.h"

namespace CemuUwp::NetGame
{
namespace
{
	std::string s_lastError;

	// libsmb2 (include/smb2/libsmb2.h), the few calls used; the library is loaded at run time
	struct smb2_context;
	struct smb2fh;
	struct Smb2
	{
		smb2_context* (*init_context)();
		void (*destroy_context)(smb2_context*);
		void (*set_timeout)(smb2_context*, int);
		void (*set_security_mode)(smb2_context*, uint16_t);
		void (*set_domain)(smb2_context*, const char*);
		void (*set_password)(smb2_context*, const char*);
		int (*connect_share)(smb2_context*, const char*, const char*, const char*);
		int (*disconnect_share)(smb2_context*);
		smb2fh* (*open)(smb2_context*, const char*, int);
		int (*close)(smb2_context*, smb2fh*);
		int (*pread)(smb2_context*, smb2fh*, uint8_t*, uint32_t, uint64_t);
		int64_t (*lseek)(smb2_context*, smb2fh*, int64_t, int, uint64_t*);
		uint32_t (*get_max_read_size)(smb2_context*);
		const char* (*get_error)(smb2_context*);
	};
	constexpr uint16_t kSigningEnabled = 0x0001; // SMB2_NEGOTIATE_SIGNING_ENABLED

	const Smb2* LoadSmb2()
	{
		static Smb2 api;
		static bool tried = false, ok = false;
		if (tried)
			return ok ? &api : nullptr;
		tried = true;
		HMODULE m = LoadPackagedLibrary(L"smb2.dll", 0);
		if (!m)
		{
			s_lastError = fmt::format("smb2.dll could not be loaded ({})", GetLastError());
			cemuLog_log(LogType::Force, "network: {}", s_lastError);
			return nullptr;
		}
		auto get = [&](auto& fn, const char* name) {
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(m, name));
			return fn != nullptr;
		};
		ok = get(api.init_context, "smb2_init_context") && get(api.destroy_context, "smb2_destroy_context") &&
			 get(api.set_timeout, "smb2_set_timeout") && get(api.set_security_mode, "smb2_set_security_mode") &&
			 get(api.set_domain, "smb2_set_domain") && get(api.set_password, "smb2_set_password") &&
			 get(api.connect_share, "smb2_connect_share") && get(api.disconnect_share, "smb2_disconnect_share") &&
			 get(api.open, "smb2_open") && get(api.close, "smb2_close") && get(api.pread, "smb2_pread") &&
			 get(api.lseek, "smb2_lseek") && get(api.get_max_read_size, "smb2_get_max_read_size") &&
			 get(api.get_error, "smb2_get_error");
		if (!ok)
			cemuLog_log(LogType::Force, "network: smb2.dll is missing a function");
		else
		{
			WSADATA wsa;
			WSAStartup(MAKEWORD(2, 2), &wsa);
		}
		return ok ? &api : nullptr;
	}

	struct Cred
	{
		std::string server, share, user, password;
	};

	void Wipe(std::string& s)
	{
		std::fill(s.begin(), s.end(), ' ');
		s.clear();
	}

	// the sign-in for server\share from Game Vault's file; the file is gone from the disk at once (written over, deleted)
	Cred ReadCred(const std::wstring& file, const std::string& server, const std::string& share)
	{
		Cred found{server, share, "Guest", ""};
		if (file.empty())
			return found;
		std::string text;
		{
			std::ifstream in(std::filesystem::path(file), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			text = ss.str();
		}
		if (!text.empty())
		{
			std::ofstream out(std::filesystem::path(file), std::ios::binary | std::ios::trunc);
			out << std::string(text.size(), ' ');
		}
		DeleteFileW(file.c_str());
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			std::vector<std::string> f;
			size_t at = 0;
			for (int i = 0; i < 3; i++)
			{
				size_t tab = line.find('\t', at);
				if (tab == std::string::npos)
					break;
				f.push_back(line.substr(at, tab - at));
				at = tab + 1;
			}
			f.push_back(line.substr(at));
			if (f.size() == 4 && !_stricmp(f[0].c_str(), server.c_str()) && !_stricmp(f[1].c_str(), share.c_str()))
			{
				found.user = f[2].empty() ? "Guest" : f[2];
				found.password = f[3];
			}
			for (auto& s : f)
				Wipe(s);
			Wipe(line);
		}
		Wipe(text);
		return found;
	}

	class SmbFile : public FileStreamNetSource
	{
	  public:
		static constexpr uint64_t kChunk = 1 << 20; // cache piece
		static constexpr size_t kSlots = 32;		// 32 MB of them

		SmbFile(const Smb2* api, Cred cred, std::string path) : m_api(api), m_cred(std::move(cred)), m_path(std::move(path)) {}
		~SmbFile() override
		{
			Drop();
			Wipe(m_cred.password);
		}

		bool Connect()
		{
			Drop();
			std::string user = m_cred.user, domain;
			size_t slash = user.find('\\');
			if (slash != std::string::npos) // DOMAIN\user
			{
				domain = user.substr(0, slash);
				user = user.substr(slash + 1);
			}
			m_ctx = m_api->init_context();
			if (!m_ctx)
				return false;
			m_api->set_timeout(m_ctx, 15);
			m_api->set_security_mode(m_ctx, kSigningEnabled);
			if (!domain.empty())
				m_api->set_domain(m_ctx, domain.c_str());
			m_api->set_password(m_ctx, m_cred.password.c_str());
			if (m_api->connect_share(m_ctx, m_cred.server.c_str(), m_cred.share.c_str(), user.c_str()) < 0)
			{
				s_lastError = fmt::format("{}\\{} as {}: {}", m_cred.server, m_cred.share, user, m_api->get_error(m_ctx));
				cemuLog_log(LogType::Force, "network: {}", s_lastError);
				m_api->destroy_context(m_ctx);
				m_ctx = nullptr;
				return false;
			}
			m_fh = m_api->open(m_ctx, m_path.c_str(), O_RDONLY);
			if (!m_fh)
			{
				s_lastError = fmt::format("{} could not be opened: {}", m_path, m_api->get_error(m_ctx));
				Drop();
				return false;
			}
			uint64_t end = 0;
			if (m_api->lseek(m_ctx, m_fh, 0, SEEK_END, &end) < 0)
			{
				Drop();
				return false;
			}
			m_size = end;
			m_maxRead = std::max<uint32_t>(64 * 1024, std::min<uint32_t>(m_api->get_max_read_size(m_ctx), kChunk));
			return true;
		}

		uint64 Size() override { return m_size; }

		bool Read(uint64 offset, void* buffer, size_t length) override
		{
			if (offset > m_size || length > m_size - offset)
				return false;
			std::lock_guard<std::mutex> lock(m_lock);
			auto* out = static_cast<uint8_t*>(buffer);
			while (length)
			{
				uint64_t chunk = offset / kChunk;
				Slot* slot = Fetch(chunk);
				if (!slot)
					return false;
				size_t in = size_t(offset - chunk * kChunk);
				size_t n = std::min<size_t>(length, slot->data.size() - in);
				if (!n)
					return false;
				std::memcpy(out, slot->data.data() + in, n);
				out += n;
				offset += n;
				length -= n;
			}
			return true;
		}

	  private:
		struct Slot
		{
			int64_t chunk = -1;
			uint64_t used = 0;
			std::vector<uint8_t> data;
		};

		void Drop()
		{
			if (m_ctx)
			{
				if (m_fh)
					m_api->close(m_ctx, m_fh);
				m_api->disconnect_share(m_ctx);
				m_api->destroy_context(m_ctx);
			}
			m_ctx = nullptr;
			m_fh = nullptr;
		}

		bool ReadNet(uint64_t at, uint8_t* buf, size_t len)
		{
			size_t done = 0;
			while (done < len)
			{
				uint32_t n = uint32_t(std::min<size_t>(len - done, m_maxRead));
				int got = m_ctx && m_fh ? m_api->pread(m_ctx, m_fh, buf + done, n, at + done) : -1;
				if (got <= 0)
					return false;
				done += size_t(got);
			}
			return true;
		}

		Slot* Fetch(uint64_t chunk)
		{
			Slot* pick = nullptr;
			for (auto& s : m_slots)
			{
				if (s.chunk == int64_t(chunk))
				{
					s.used = ++m_clock;
					return &s;
				}
				if (!pick || s.used < pick->used)
					pick = &s; // the empty one, or the one read longest ago
			}
			uint64_t at = chunk * kChunk;
			size_t len = size_t(std::min<uint64_t>(kChunk, m_size - at));
			pick->chunk = -1;
			pick->data.resize(len);
			if (!ReadNet(at, pick->data.data(), len))
			{
				// once more on a fresh connection: the PC slept, the Wi-Fi blinked
				cemuLog_log(LogType::Force, "network: read at {:x} failed ({}), connecting again", at,
							m_ctx ? m_api->get_error(m_ctx) : "no connection");
				if (!Connect() || !ReadNet(at, pick->data.data(), len))
				{
					cemuLog_log(LogType::Force, "network: the game could not be read at {:x}", at);
					return nullptr;
				}
			}
			pick->chunk = int64_t(chunk);
			pick->used = ++m_clock;
			// how the reading goes: the first reads, then every 64 MB (a start that stalls shows whether the
			// network is still being read)
			m_fetched += len;
			if (++m_fetches <= 4 || m_fetched >= m_nextReport)
			{
				m_nextReport = m_fetched + (64ull << 20);
				cemuLog_log(LogType::Force, "network: read {:x} ({} pieces, {} MB so far)", at, m_fetches, m_fetched >> 20);
			}
			return pick;
		}

		const Smb2* m_api;
		Cred m_cred;
		std::string m_path;
		smb2_context* m_ctx = nullptr;
		smb2fh* m_fh = nullptr;
		uint64_t m_size = 0;
		uint32_t m_maxRead = 64 * 1024;
		std::mutex m_lock;
		Slot m_slots[kSlots];
		uint64_t m_clock = 0;
		uint64_t m_fetched = 0, m_fetches = 0, m_nextReport = 0;
	};

	// a small file read whole (a disc key next to the image)
	class MemoryFile : public FileStreamNetSource
	{
	  public:
		explicit MemoryFile(std::vector<uint8_t> data) : m_data(std::move(data)) {}
		uint64 Size() override { return m_data.size(); }
		bool Read(uint64 offset, void* buffer, size_t length) override
		{
			if (offset > m_data.size() || length > m_data.size() - offset)
				return false;
			std::memcpy(buffer, m_data.data() + offset, length);
			return true;
		}

	  private:
		std::vector<uint8_t> m_data;
	};

	std::wstring Wide(const std::string& utf8)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
		std::wstring w(size_t(std::max(n, 0)), L'\0');
		if (n > 0)
			MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), w.data(), n);
		return w;
	}
}

std::string LastError()
{
	return s_lastError;
}

std::filesystem::path Open(const std::string& url, const std::wstring& credFile)
{
	s_lastError.clear();
	// "smb://server/share/a/b.wux" -> server, share, "a\b.wux" (libsmb2 sends the path as it is: Windows separators)
	if (url.rfind("smb://", 0) != 0)
		return {};
	size_t a = url.find('/', 6);
	size_t b = a == std::string::npos ? a : url.find('/', a + 1);
	if (a == std::string::npos || a == 6 || b == std::string::npos || b == a + 1 || b + 1 >= url.size())
	{
		cemuLog_log(LogType::Force, "network: not a network game: {}", url);
		return {};
	}
	std::string server = url.substr(6, a - 6), share = url.substr(a + 1, b - a - 1), path = url.substr(b + 1);
	std::replace(path.begin(), path.end(), '/', '\\');
	const Smb2* api = LoadSmb2();
	Cred cred = ReadCred(credFile, server, share);
	if (cred.user == "Guest" && !credFile.empty())
		cemuLog_log(LogType::Force, "network: no sign-in for {}\\{} in Game Vault's file", server, share);
	if (!api)
	{
		Wipe(cred.password);
		return {};
	}
	Cred keyCred = cred;
	auto game = std::make_shared<SmbFile>(api, std::move(cred), path);
	DWORD start = GetTickCount();
	if (!game->Connect())
	{
		cemuLog_log(LogType::Force, "network: {}\\{}\\{} could not be opened", server, share, path);
		Wipe(keyCred.password);
		return {};
	}
	cemuLog_log(LogType::Force, "network: {}\\{}\\{} opened in {} ms, {} bytes", server, share, path, GetTickCount() - start, game->Size());
	// a stand-in path that names no real file: FileStream finds the reader by it (the name keeps the extension,
	// which tells Cemu the format)
	std::string name = path.substr(path.find_last_of('\\') + 1);
	std::filesystem::path standIn = std::filesystem::path(L"N:\\Game Vault network") / Wide(name);
	FileStream::RegisterNetFile(standIn, game);
	// a disc key next to the image ("<game>.key"), which Cemu looks for when keys.txt has none for the disc
	size_t dot = path.find_last_of('.');
	if (dot != std::string::npos && dot > path.find_last_of('\\'))
	{
		auto key = std::make_shared<SmbFile>(api, std::move(keyCred), path.substr(0, dot) + ".key");
		std::vector<uint8_t> bytes(16);
		if (key->Connect() && key->Size() == 16 && key->Read(0, bytes.data(), 16))
		{
			std::filesystem::path keyPath = standIn;
			keyPath.replace_extension("key");
			FileStream::RegisterNetFile(keyPath, std::make_shared<MemoryFile>(std::move(bytes)));
			cemuLog_log(LogType::Force, "network: disc key file found next to the game");
		}
	}
	else
		Wipe(keyCred.password);
	return standIn;
}

}
