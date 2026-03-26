#include "msxplay.h"
#include <stdio.h>
#include <string.h>

static SDL_AudioDeviceID audioDevice;

typedef struct {
    MixerUpdateCallback callback;
    void* param;
} MixerChannel;

static MixerChannel channels[16];
static int channelCount = 0;

extern "C" Int32 mixerRegisterChannel(Mixer* mixer, Int32 audioType, Int32 stereo, 
                           MixerUpdateCallback callback, MixerSetSampleRateCallback rateCallback,
                           void* param) {
    if (channelCount >= 16) return -1;
    if (debugMode) printf("mixerRegisterChannel: type=%d, param=%p\n", audioType, (void*)param);
    channels[channelCount].callback = callback;
    channels[channelCount].param = param;
    if (rateCallback) rateCallback(param, 44100);
    return channelCount++;
}

extern "C" void mixerUnregisterChannel(Mixer* mixer, Int32 handle) {}
extern "C" void mixerSync(Mixer* mixer) {}

void initSound() {
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    
    audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audioDevice == 0) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audioDevice, 0);
    }
}

extern "C" void clearQueuedAudio(void) {
    if (audioDevice != 0)
        SDL_ClearQueuedAudio(audioDevice);
}

extern "C" void updateSound() {
    if (audioDevice == 0) return;

    int samplesPerFrame = 44100 / 60;
    static Int16 outBuf[44100 / 60];
    memset(outBuf, 0, sizeof(outBuf));

    static int frameCount = 0;
    long totalAbs = 0;

    for (int i = 0; i < channelCount; i++) {
        Int32* channelBuf = channels[i].callback(channels[i].param, samplesPerFrame);
        if (channelBuf) {
            for (int s = 0; s < samplesPerFrame; s++) {
                Int32 val = outBuf[s] + (channelBuf[s] >> 4);
                if (val > 32767) val = 32767;
                if (val < -32768) val = -32768;
                outBuf[s] = (Int16)val;
                totalAbs += (val > 0 ? val : -val);
            }
        }
    }

    if (debugMode && frameCount++ % 120 == 0 && totalAbs > 0) {
        printf("updateSound: avg amplitude %ld\n", totalAbs / samplesPerFrame);
        fflush(stdout);
    } else { frameCount++; }

    if (SDL_GetQueuedAudioSize(audioDevice) < (Uint32)samplesPerFrame * 4) {
        SDL_QueueAudio(audioDevice, outBuf, samplesPerFrame * sizeof(Int16));
    }
}
