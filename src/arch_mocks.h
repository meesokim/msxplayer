
#ifndef ARCH_MOCKS_H
#define ARCH_MOCKS_H

#include "MsxTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

void* archEventCreate(int initState);
void archEventDestroy(void* event);
void archEventSet(void* event);
void archEventWait(void* event, int timeout);

int archVideoInIsVideoConnected();
UInt16* archVideoInBufferGet(int width, int height);
void archVideoCaptureSave();

#ifdef __cplusplus
}
#endif

#endif
