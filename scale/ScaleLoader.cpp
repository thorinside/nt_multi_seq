#include "scale/ScaleLoader.h"

static void scaleLoaderCallback(void* callbackData)
{
    ScaleLoader* loader = static_cast<ScaleLoader*>(callbackData);
    loader->awaitingCallback = false;
    loader->dirty = true;
}

void ScaleLoader::init()
{
    cardMounted = false;
    awaitingCallback = false;
    dirty = false;
    name[0] = 0;
    description[0] = 0;

    request.notes = notes;
    request.maxNotes = kMaxNotes;
    request.nameBuffer = name;
    request.nameBufferSize = sizeof(name);
    request.descriptionBuffer = description;
    request.descriptionBufferSize = sizeof(description);
    request.callback = scaleLoaderCallback;
    request.callbackData = this;
}

void ScaleLoader::requestScale(int index)
{
    if (awaitingCallback)
        return;
    request.index = index;
    awaitingCallback = true;
    if (!NT_readScl(request))
        awaitingCallback = false;
}

bool ScaleLoader::poll(_NT_algorithm* self, _NT_parameter& scaleFileParam, int scaleFileParamIndex)
{
    bool mounted = NT_isSdCardMounted();
    if (cardMounted != mounted) {
        cardMounted = mounted;
        if (mounted) {
            int numScales = NT_getNumScl();
            if (numScales > 0) {
                scaleFileParam.max = numScales - 1;
                int algIdx = NT_algorithmIndex(self);
                if (algIdx >= 0)
                    NT_updateParameterDefinition(algIdx, scaleFileParamIndex);
            }
            requestScale(self->v[scaleFileParamIndex]);
        } else {
            awaitingCallback = false;
        }
    }

    if (!dirty)
        return false;
    dirty = false;

    if (request.error || request.numNotes == 0)
        return false;
    quantizer.loadScale(notes, request.numNotes);
    return true;
}
