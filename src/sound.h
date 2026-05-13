#ifndef PONGWIN32_SOUND_H_
#define PONGWIN32_SOUND_H_

#include "framework.h"

// Default BGM filename (side-by-side with the .exe). Ignored when
// kUseEmbeddedBgm is true, but kept for the fallback file-source path.
inline const std::wstring sound_file = L"music.wav";

// Compile-time switch for how the background music is sourced. When true,
// PlayWavFile ignores its wav_file argument and plays the WAV baked into
// the .exe as the IDR_BGM_WAV resource (extracted to a temp file so MCI
// can open it - MCI's string API has no "play from memory" form, and we
// need MCI specifically to decode MS ADPCM reliably on Win2K). When false,
// PlayWavFile reads the file by name from the exe directory as before.
// Flip here and rebuild; callers pass this through to PlayWavFile.
inline constexpr bool kUseEmbeddedBgm = true;

// User preference (declared in globals.h, defined in sound.cc): true if
// the user wants sound enabled. Toggled via IDM_SOUND / SetSoundOn. Audio
// actually plays only when this is true AND the game isn't paused - see
// SyncBgm below; it's the one place that starts and stops the music.

// Plays a .wav file. On the first call, opens the file and starts playback
// from position 0. On subsequent calls after a PauseWavFile, issues an MCI
// `resume` so playback continues from where it was paused (no re-open, no
// restart).
//
// If use_embedded is true, wav_file is ignored and the clip is taken from
// the IDR_BGM_WAV resource baked into the exe. On first play the resource
// is materialized to a temp file (MCI needs a path, not a memory buffer)
// and StopPlayWav deletes that file on cleanup. If use_embedded is false,
// wav_file must name a .wav sitting next to the exe.
bool PlayWavFile(const std::wstring& wav_file, bool use_embedded);

// Pauses the currently-playing sound, preserving playback position. The
// MCI device stays open; a following PlayWavFile will resume rather than
// restart. Used by SyncBgm on the pause / mute transitions.
bool PauseWavFile();

// Fully stops playback and releases the MCI device (stop + close). Used by
// ShutDownApp for cleanup on exit - not by the mute toggle.
bool StopPlayWav();

// The one place that starts or stops audio, so playback always matches
// the rule "audio plays when g_sound_on is true AND the game isn't
// paused". If audio is already in the right state, this does nothing.
// Call after any change to g_sound_on or g_paused (and once at startup,
// after the menu defaults are loaded). Returns true on success (or when
// nothing needed to change); returns false only when starting playback
// failed (MCI open or play error).
bool SyncBgm();

// Spins up the BGM worker thread. The worker owns the MCI device
// end-to-end - every mciSendString (open, play, pause, resume, stop,
// close, and the loop re-issue) runs on this thread, including the
// MM_MCINOTIFY loop-back that drives looping. Main thread interacts
// only through the public API above, which posts commands to a small
// single-slot queue and waits for the worker to finish. Call once at
// app startup, before any PlayWavFile. Returns true on success, false
// if any of the events / worker thread / worker init failed (callers
// can warn the user that BGM will be unavailable).
bool InitBgm();

// Signals the BGM worker to exit and joins it, releasing the worker's
// hidden notify window and synchronization primitives. Call at app
// shutdown AFTER StopPlayWav so the stop command can still be processed
// by a live worker before teardown.
void ShutDownBgm();

#endif // PONGWIN32_SOUND_H_
