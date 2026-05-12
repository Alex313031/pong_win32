#ifndef PONGWIN32_STRINGS_H_
#define PONGWIN32_STRINGS_H_

#include "version.h"

// Strings to print in game message area

inline const std::wstring kLoseMsg = L"You Lose... \nPress F2 to Play Again.";

inline const std::wstring kWinMsg = L"You Win!";

inline const std::wstring kNewGameMsg = L"New Game";

inline const std::wstring kPausedMsg = L"Paused";

inline const std::wstring kToggleMsg = L"Toggled Players";

inline const std::wstring kMuteMsg = L"Muted Sound";

inline const std::wstring kUnMuteMsg = L"UnMuted Sound";

// Adjacent literal concatenation (like ABOUT_VERSION in version.h), not
// operator+ - the latter would be pointer-arithmetic on two wchar_t*.
inline const std::wstring kReadyMsg = L"Welcome to " APP_NAME L" " VERSION_STRING;

#endif // PONGWIN32_STRINGS_H_
