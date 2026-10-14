/* Exercise the actual SDL queue without a device or an audio thread.
   Catch short cushions and loss of PCM during normal catch-up bursts. */
#include "../out_sdl.c"

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, CHANNELS, 48000};
    int16_t pcm[800 * CHANNELS], received[(QUEUE_TARGET_FRAMES + 800) * CHANNELS];
    stream = SDL_CreateAudioStream(&spec, &spec);
    if (!stream) {
        fprintf(stderr, "SDL stream: %s\n", SDL_GetError());
        return 1;
    }
    for (int i = 0; i < 800 * CHANNELS; i++) {
        pcm[i] = (int16_t)(i + 1);
    }
    ico_audio_set_volume(1.0);
    ico_audio_sdl_push(pcm, 800);
    CHECK(underruns == 1 && drops == 0);
    CHECK(SDL_GetAudioStreamQueued(stream) == (QUEUE_TARGET_FRAMES + 800) * BYTES_PER_FRAME);
    CHECK(ico_audio_sdl_active());
    CHECK(SDL_GetAudioStreamData(stream, received, sizeof(received)) == sizeof(received));
    for (int i = 0; i < QUEUE_TARGET_FRAMES * CHANNELS; i++) {
        CHECK(received[i] == 0);
    }
    CHECK(memcmp(received + QUEUE_TARGET_FRAMES * CHANNELS, pcm, sizeof(pcm)) == 0);

    /* Six vsyncs caught up after a stall must retain every block. The old
       four-vsync ceiling discarded audio even below 100 ms of backlog. */
    SDL_ClearAudioStream(stream);
    buffer_silence(QUEUE_TARGET_FRAMES);
    for (int i = 0; i < 6; i++) {
        ico_audio_sdl_push(pcm, 800);
    }
    CHECK(drops == 0);
    CHECK(SDL_GetAudioStreamQueued(stream) == (QUEUE_TARGET_FRAMES + 4800) * BYTES_PER_FRAME);

    /* Bound excessive latency without discarding a normally buffered block. */
    SDL_ClearAudioStream(stream);
    buffer_silence(QUEUE_LIMIT_FRAMES + 1);
    ico_audio_sdl_push(pcm, 800);
    CHECK(drops == 1);
    CHECK(SDL_GetAudioStreamQueued(stream) == (QUEUE_LIMIT_FRAMES + 1) * BYTES_PER_FRAME);
    ico_audio_sdl_push(pcm, 0);
    CHECK(drops == 1);
    SDL_DestroyAudioStream(stream);
    stream = NULL;
    CHECK(!ico_audio_sdl_active());
    return failures ? 1 : 0;
}
