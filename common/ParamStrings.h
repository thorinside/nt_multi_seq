#ifndef PARAM_STRINGS_H
#define PARAM_STRINGS_H

// Enum string tables and parameterString() helpers shared by every algorithm
// in this plugin. Tables are defined once (common/ParamStrings.cpp) so they
// occupy a single .rodata copy instead of one per translation unit.

extern const char* const kOffOnStrings[];    // "Off", "On", nullptr
extern const char* const kRootNoteNames[];   // "C" .. "B" (12 entries)

static constexpr int kNumRootNotes = 12;

// Copies s into a parameterString() buffer, truncating to the host's limit.
// Returns the copied length.
int copyParameterString(char* buff, const char* s);

// Formats a Root Note parameter value (0..11). Returns 0 if out of range.
int rootNoteParameterString(int v, char* buff);

#endif // PARAM_STRINGS_H
