// Copyright (c) 2026 Skitzelles - part of Cemu for Game Vault (github.com/skitzellez/xbox-game-vault).
// Licensed like the rest of Cemu (MPL-2.0).
#pragma once

#include <filesystem>
#include <string>

namespace CemuUwp::NetGame
{
	// Game Vault sends "smb://<server>/<share>/<path in the share>" as the game and netcred=<file>: a file in the
	// folder only Game Vault and its emulators can open, one line per share "server<tab>share<tab>user<tab>password".
	// The file is read once, written over and deleted. The game is then read over the network (libsmb2, smb2.dll)
	// through FileStream under the stand-in path returned here; empty when it cannot be opened.
	std::filesystem::path Open(const std::string& url, const std::wstring& credFile);

	// why the last Open failed, for the message on screen (Cemu's log is not running yet at a link start)
	std::string LastError();
}
