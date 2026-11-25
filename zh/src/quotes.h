#pragma once
#include <string>
#include <vector>

// Loads Chinese quote sentences from an .xlsx (sharedStrings) or a UTF-8 .txt file.
// Returns false when the file is missing or unreadable; the caller simply shows no quotes.
bool LoadQuoteFile(const std::wstring& path, std::vector<std::wstring>& out);
