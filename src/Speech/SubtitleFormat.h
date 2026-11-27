// MeetRec - subtitle/text formatting for meeting transcripts (P1).
//
// Pure helpers shared by the transcript writer, the speech session and the
// tests: SRT timestamps, speaker labels, line wrapping and the defensive tag
// cleanup for SenseVoice's rich-transcription output.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vsr::speech {

// "HH:MM:SS,mmm" for SRT; negative input clamps to zero.
std::wstring FormatSrtTimestamp(std::int64_t microseconds);

// "[HH:MM:SS]" for the plain-text transcript.
std::wstring FormatTranscriptClock(std::int64_t microseconds);

// "对方"/"我" (or "Them"/"Me"); the microphone is always the local speaker.
std::wstring SpeakerLabel(bool microphone, bool chinese);

// Label plus separator ("对方：" / "Them: ").
std::wstring SpeakerPrefix(bool microphone, bool chinese);

// Removes SenseVoice rich-transcription tags (<|zh|>, <|HAPPY|>, ...), folds
// CR/LF/tabs to spaces and trims. Empty when nothing but tags/space remains.
std::wstring SanitizeTranscriptText(const std::wstring& text);

// Greedy display-width wrapping: CJK/fullwidth glyphs count as two units,
// Latin as one. Prefers punctuation/space boundaries. Returns at least one
// line for non-empty text (up to `maxUnits` units per line).
std::vector<std::wstring> WrapSubtitleText(const std::wstring& text,
                                           std::size_t maxUnits = 42);

} // namespace vsr::speech
