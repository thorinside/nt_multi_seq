#include "common/ParamStrings.h"
#include <distingnt/api.h>
#include <string.h>

const char* const kOffOnStrings[] = { "Off", "On", nullptr };

const char* const kRootNoteNames[] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

int copyParameterString(char* buff, const char* s)
{
    strncpy(buff, s, kNT_parameterStringSize - 1);
    buff[kNT_parameterStringSize - 1] = 0;
    return static_cast<int>(strlen(buff));
}

int rootNoteParameterString(int v, char* buff)
{
    if (v < 0 || v >= kNumRootNotes)
        return 0;
    return copyParameterString(buff, kRootNoteNames[v]);
}
