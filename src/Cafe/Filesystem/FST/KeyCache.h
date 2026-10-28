#pragma once

void KeyCache_Prepare();
bool KeyCache_AddKeysFile(const fs::path& keysPath);

uint8* KeyCache_GetAES128(sint32 index);