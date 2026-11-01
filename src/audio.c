/*
 * s3eSound (PCM channels, 22 kHz mono) and s3eAudio (MP3 music) on one SDL
 * output stream. The SDL thread only mixes host-owned buffers; every guest
 * callback (generators, end-of-sample, stop) runs from audio_pump on the
 * emulation thread.
 */
#include "host.h"

#include <SDL.h>
#include <stdlib.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/dr_mp3.h"

enum {
    OUTPUT_RATE = 44100,
    S3E_MAX_VOLUME = 256,
    SOUND_CHANNELS = 24,
    AUDIO_CHANNELS = 2,
    SOUND_FREQUENCY = 22050,
    SOUND_RATE_MAX = 0x40000,
    GENERATOR_CHUNK = 4096,
    MUSIC_BUFFER_FRAMES = 2048,
    GENERATOR_MAX_SAMPLES = 22050 * 60 * 4,
    S3E_CHANNEL_END_SAMPLE = 0,
    S3E_CHANNEL_GEN_AUDIO = 1,
    S3E_CHANNEL_STOP_AUDIO = 2,
    S3E_CHANNEL_CALLBACK_COUNT = 4,
    S3E_AUDIO_STOP = 2,
    S3E_AUDIO_CALLBACK_COUNT = 3,
    AUDIO_STATUS_STOPPED = 0,
    AUDIO_STATUS_PLAYING = 1,
    AUDIO_STATUS_PAUSED = 2,
};

struct guest_callback {
    uint32_t fn;
    uint32_t user_data;
};

struct sound_channel {
    /* mixer-owned while playing */
    int16_t *pcm; /* host copy of the samples being played */
    uint32_t samples;
    uint64_t pos; /* 32.32 fixed point sample position */
    int playing;
    int paused;
    int finished; /* set by the mixer when the data ran out */
    int volume;   /* 0..256 */
    int rate;     /* Hz */
    /* emulation-thread state */
    struct guest_callback callbacks[S3E_CHANNEL_CALLBACK_COUNT];
    int rate_scale;
    int user_value;
    uint32_t source; /* guest data, for repeats */
    uint32_t source_samples;
    uint32_t reps_remaining;
    uint32_t loop_index;
};

struct music_channel {
    drmp3 mp3;
    uint8_t *data;
    int open;
    int paused;
    int finished;
    uint32_t repeat; /* 0 = forever */
    int volume;
    int16_t buffer[MUSIC_BUFFER_FRAMES * 2];
    uint32_t buffer_frames, buffer_pos;
    int empty_reads;
    uint64_t pos; /* 32.32 position between prev and next */
    int16_t prev[2], next[2];
    int primed;
};

static SDL_AudioDeviceID g_device;
static struct sound_channel g_sound[SOUND_CHANNELS];
static struct music_channel g_music[AUDIO_CHANNELS];
static struct guest_callback g_audio_callbacks[S3E_AUDIO_CALLBACK_COUNT];
static int g_music_channel;
static int g_sound_volume = S3E_MAX_VOLUME;
static int g_sound_rate = SOUND_FREQUENCY;
static int g_pumping;

static void lock(void) {
    if (g_device) {
        SDL_LockAudioDevice(g_device);
    }
}

static void unlock(void) {
    if (g_device) {
        SDL_UnlockAudioDevice(g_device);
    }
}

static int clamp_volume(int v) {
    return v < 0 ? 0 : v > S3E_MAX_VOLUME ? S3E_MAX_VOLUME : v;
}

static int16_t clamp16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

static void mix_sound(int32_t *out, int frames) {
    for (int c = 0; c < SOUND_CHANNELS; ++c) {
        struct sound_channel *ch = &g_sound[c];
        if (!ch->playing || ch->paused || !ch->pcm) {
            continue;
        }
        uint64_t step = ((uint64_t)(ch->rate > 0 ? ch->rate : SOUND_FREQUENCY) << 32) / OUTPUT_RATE;
        int vol = ch->volume * g_sound_volume / S3E_MAX_VOLUME;
        for (int i = 0; i < frames; ++i) {
            uint32_t idx = (uint32_t)(ch->pos >> 32);
            if (idx >= ch->samples) {
                ch->playing = 0;
                ch->finished = 1;
                break;
            }
            /* linear interpolation between neighbouring samples */
            int32_t a = ch->pcm[idx];
            int32_t b = idx + 1 < ch->samples ? ch->pcm[idx + 1] : a;
            int32_t frac = (int32_t)((ch->pos >> 16) & 0xffff);
            int32_t s = a + (((b - a) * frac) >> 16);
            s = s * vol / S3E_MAX_VOLUME;
            out[i * 2] += s;
            out[i * 2 + 1] += s;
            ch->pos += step;
        }
    }
}

/* Next decoded frame as stereo; false at the end of the file (no more repeats). */
static bool music_next_frame(struct music_channel *m, int16_t frame[2]) {
    int channels = (int)m->mp3.channels;
    while (m->buffer_pos >= m->buffer_frames) {
        drmp3_uint64 got = drmp3_read_pcm_frames_s16(&m->mp3, MUSIC_BUFFER_FRAMES, m->buffer);
        if (got == 0) {
            if (m->repeat == 1) {
                return false;
            }
            if (m->repeat > 1) {
                m->repeat--;
            }
            if (m->empty_reads++ > 0 || !drmp3_seek_to_pcm_frame(&m->mp3, 0)) {
                return false; /* undecodable: do not spin in the audio thread */
            }
            continue;
        }
        m->empty_reads = 0;
        m->buffer_frames = (uint32_t)got;
        m->buffer_pos = 0;
    }
    const int16_t *in = m->buffer + m->buffer_pos++ * (uint32_t)channels;
    frame[0] = in[0];
    frame[1] = channels > 1 ? in[1] : in[0];
    return true;
}

/* The game ships 16 kHz and 48 kHz files: resample to the output rate (linear). */
static void mix_music(int32_t *out, int frames) {
    for (int c = 0; c < AUDIO_CHANNELS; ++c) {
        struct music_channel *m = &g_music[c];
        if (!m->open || m->paused || m->finished) {
            continue;
        }
        uint32_t rate = m->mp3.sampleRate ? m->mp3.sampleRate : OUTPUT_RATE;
        uint64_t step = ((uint64_t)rate << 32) / OUTPUT_RATE;
        if (!m->primed) {
            if (!music_next_frame(m, m->next)) {
                m->finished = 1;
                continue;
            }
            m->prev[0] = m->next[0];
            m->prev[1] = m->next[1];
            m->primed = 1;
        }
        for (int i = 0; i < frames; ++i) {
            int32_t frac = (int32_t)(m->pos >> 16);
            int32_t l = m->prev[0] + (((m->next[0] - m->prev[0]) * frac) >> 16);
            int32_t r = m->prev[1] + (((m->next[1] - m->prev[1]) * frac) >> 16);
            out[i * 2] += l * m->volume / S3E_MAX_VOLUME;
            out[i * 2 + 1] += r * m->volume / S3E_MAX_VOLUME;
            m->pos += step;
            bool ended = false;
            while (m->pos >> 32) {
                m->pos -= (uint64_t)1 << 32;
                m->prev[0] = m->next[0];
                m->prev[1] = m->next[1];
                if (!music_next_frame(m, m->next)) {
                    ended = true;
                    break;
                }
            }
            if (ended) {
                m->finished = 1;
                break;
            }
        }
    }
}

static void audio_callback(void *user, uint8_t *stream, int length) {
    (void)user;
    int frames = length / 4;
    int32_t *mix = calloc((size_t)frames * 2, sizeof(int32_t));
    if (!mix) {
        memset(stream, 0, (size_t)length);
        return;
    }
    mix_sound(mix, frames);
    mix_music(mix, frames);
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < frames * 2; ++i) {
        out[i] = clamp16(mix[i]);
    }
    free(mix);
}

bool audio_init(void) {
    for (int c = 0; c < SOUND_CHANNELS; ++c) {
        g_sound[c].volume = S3E_MAX_VOLUME;
        g_sound[c].rate = SOUND_FREQUENCY;
        g_sound[c].rate_scale = 0x10000;
    }
    for (int c = 0; c < AUDIO_CHANNELS; ++c) {
        g_music[c].volume = S3E_MAX_VOLUME;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        log_error("SDL audio: %s (continuing without sound)", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want = {0}, have;
    want.freq = OUTPUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    g_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_device) {
        log_error("audio device: %s (continuing without sound)", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(g_device, 0);
    return true;
}

void audio_shutdown(void) {
    if (g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
    }
}

static uint32_t call(struct guest_callback *cb, uint32_t system_data) {
    return cb->fn ? guest_call2(cb->fn, system_data, cb->user_data) : 0;
}

/* Run the game's sample generator (its ADPCM/ogg decoder) to completion. */
static int16_t *generate(int channel, uint32_t data, uint32_t samples, uint32_t *out_samples) {
    struct sound_channel *ch = &g_sound[channel];
    uint32_t capacity = GENERATOR_CHUNK * 4;
    uint32_t chunk = gmalloc(GENERATOR_CHUNK * 2);
    uint32_t info = gmalloc(32);
    int16_t *pcm = malloc(capacity * 2);
    uint32_t written = 0;
    for (;;) {
        uint32_t *w = G2H(info);
        w[0] = (uint32_t)channel; /* m_Channel */
        w[1] = chunk;             /* m_Target */
        w[2] = GENERATOR_CHUNK;   /* m_NumSamples */
        w[3] = 0;                 /* m_Mix */
        w[4] = data;              /* m_OrigStart */
        w[5] = samples;           /* m_OrigNumSamples */
        w[6] = 0;                 /* m_OrigRepeat */
        w[7] = 0;                 /* m_EndSample */
        int32_t produced = (int32_t)call(&ch->callbacks[S3E_CHANNEL_GEN_AUDIO], info);
        int end = w[7] != 0;
        if (produced < 0 || produced > GENERATOR_CHUNK || (produced == 0 && !end)) {
            break;
        }
        if (written + (uint32_t)produced > capacity) {
            capacity = (written + (uint32_t)produced) * 2;
            pcm = realloc(pcm, capacity * 2);
        }
        memcpy(pcm + written, G2H(chunk), (size_t)produced * 2);
        written += (uint32_t)produced;
        if (end || written > GENERATOR_MAX_SAMPLES) {
            break;
        }
    }
    gfree(chunk);
    gfree(info);
    if (!written) {
        free(pcm);
        return NULL;
    }
    *out_samples = written;
    return pcm;
}

static int start_channel(int channel, uint32_t data, uint32_t samples, uint32_t repeat,
                         uint32_t loop_index, int begin_at_loop) {
    struct sound_channel *ch = &g_sound[channel];
    uint32_t count = samples;
    int16_t *pcm;
    if (ch->callbacks[S3E_CHANNEL_GEN_AUDIO].fn) {
        pcm = generate(channel, data, samples, &count);
        if (!pcm) {
            return 1;
        }
    } else {
        pcm = malloc((size_t)samples * 2);
        memcpy(pcm, G2H(data), (size_t)samples * 2);
    }
    if (begin_at_loop) {
        if (loop_index >= count) {
            free(pcm);
            return 1;
        }
        memmove(pcm, pcm + loop_index, (size_t)(count - loop_index) * 2);
        count -= loop_index;
    }
    lock();
    int16_t *old = ch->pcm;
    ch->pcm = pcm;
    ch->samples = count;
    ch->pos = 0;
    ch->playing = 1;
    ch->paused = 0;
    ch->finished = 0;
    unlock();
    free(old);
    ch->source = data;
    ch->source_samples = samples;
    ch->reps_remaining = repeat;
    ch->loop_index = loop_index;
    return 0;
}

static void stop_channel(int channel, int notify) {
    struct sound_channel *ch = &g_sound[channel];
    lock();
    int had = ch->pcm != NULL;
    int16_t *old = ch->pcm;
    ch->pcm = NULL;
    ch->playing = 0;
    ch->finished = 0;
    unlock();
    free(old);
    if (had && notify) {
        uint32_t info = gmalloc(16);
        uint32_t *w = G2H(info);
        w[0] = (uint32_t)channel;
        w[1] = ch->reps_remaining;
        w[2] = 0;
        w[3] = ch->source_samples;
        call(&ch->callbacks[S3E_CHANNEL_STOP_AUDIO], info);
        gfree(info);
    }
}

static void service_sound(void) {
    for (int c = 0; c < SOUND_CHANNELS; ++c) {
        struct sound_channel *ch = &g_sound[c];
        lock();
        int finished = ch->finished;
        ch->finished = 0;
        unlock();
        if (!finished) {
            continue;
        }
        int forever = ch->reps_remaining == 0;
        uint32_t reps = ch->reps_remaining ? ch->reps_remaining - 1 : 0;
        uint32_t info = gmalloc(16);
        uint32_t *w = G2H(info);
        w[0] = (uint32_t)c;
        w[1] = reps;
        w[2] = 0; /* m_NewData */
        w[3] = ch->source_samples;
        uint32_t keep = ch->callbacks[S3E_CHANNEL_END_SAMPLE].fn
                            ? call(&ch->callbacks[S3E_CHANNEL_END_SAMPLE], info)
                            : (uint32_t)(forever || reps > 0);
        uint32_t new_data = w[2], new_samples = w[3];
        int32_t new_reps = (int32_t)w[1];
        gfree(info);
        if (keep) {
            uint32_t data = new_data ? new_data : ch->source;
            uint32_t samples = new_data ? new_samples : ch->source_samples;
            if (data && samples &&
                start_channel(c, data, samples, new_reps > 0 ? (uint32_t)new_reps : 0,
                              ch->loop_index, !new_data) == 0) {
                if (!forever && !new_data) {
                    ch->reps_remaining = reps;
                }
                continue;
            }
        }
        stop_channel(c, 1);
    }
}

static void music_close(int channel) {
    struct music_channel *m = &g_music[channel];
    lock();
    int was_open = m->open;
    if (m->open) {
        drmp3_uninit(&m->mp3);
    }
    uint8_t *data = m->data;
    m->data = NULL;
    m->open = 0;
    m->finished = 0;
    m->paused = 0;
    unlock();
    free(data);
    if (was_open && g_audio_callbacks[S3E_AUDIO_STOP].fn) {
        uint32_t info = gmalloc(8);
        *(uint32_t *)G2H(info) = (uint32_t)channel;
        call(&g_audio_callbacks[S3E_AUDIO_STOP], info);
        gfree(info);
    }
}

static void service_music(void) {
    for (int c = 0; c < AUDIO_CHANNELS; ++c) {
        if (g_music[c].open && g_music[c].finished) {
            music_close(c);
        }
    }
}

void audio_pump(void) {
    if (g_pumping || !g_device) {
        return;
    }
    g_pumping = 1;
    service_music();
    service_sound();
    g_pumping = 0;
}

static int music_play(uint8_t *data, uint32_t size, uint32_t repeat) {
    int c = g_music_channel;
    music_close(c);
    struct music_channel *m = &g_music[c];
    if (!drmp3_init_memory(&m->mp3, data, size, NULL)) {
        free(data);
        return 1;
    }
    lock();
    m->data = data;
    m->repeat = repeat;
    m->finished = 0;
    m->paused = 0;
    m->buffer_frames = m->buffer_pos = 0;
    m->empty_reads = 0;
    m->pos = 0;
    m->primed = 0;
    m->open = 1;
    unlock();
    return 0;
}

static uint8_t *read_file(const char *path, uint32_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = len > 0 ? malloc((size_t)len) : NULL;
    if (data && fread(data, 1, (size_t)len, file) != (size_t)len) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *size = (uint32_t)len;
    return data;
}

IMPORT(s3eAudioPlay) {
    const char *name = G2H(arg(0));
    if (!g_device || !name) {
        ret(1);
        return;
    }
    static const char *patterns[] = {"%s/%s", "%s/assets/%s", "%s/assets/data-gles1/%s"};
    char path[1200];
    uint32_t size = 0;
    uint8_t *data = NULL;
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]) && !data; ++i) {
        snprintf(path, sizeof(path), patterns[i], g_root, name);
        data = read_file(path, &size);
    }
    if (!data) {
        log_info("music not found: %s", name);
        ret(1);
        return;
    }
    log_debug("music: %s (repeat %u)", name, arg(1));
    ret((uint32_t)music_play(data, size, arg(1)));
}

IMPORT(s3eAudioPlayFromBuffer) {
    if (!g_device || !arg(0) || !arg(1)) {
        ret(1);
        return;
    }
    uint8_t *copy = malloc(arg(1));
    memcpy(copy, G2H(arg(0)), arg(1));
    ret((uint32_t)music_play(copy, arg(1), arg(2)));
}

IMPORT(s3eAudioStop) {
    music_close(g_music_channel);
    ret(0);
}

IMPORT(s3eAudioPause) {
    lock();
    g_music[g_music_channel].paused = 1;
    unlock();
    ret(0);
}

IMPORT(s3eAudioResume) {
    lock();
    g_music[g_music_channel].paused = 0;
    unlock();
    ret(0);
}

static int music_status(void) {
    struct music_channel *m = &g_music[g_music_channel];
    if (!m->open || m->finished) {
        return AUDIO_STATUS_STOPPED;
    }
    return m->paused ? AUDIO_STATUS_PAUSED : AUDIO_STATUS_PLAYING;
}

IMPORT(s3eAudioIsPlaying) {
    ret(music_status() == AUDIO_STATUS_PLAYING);
}

IMPORT(s3eAudioGetInt) {
    switch (arg(0)) {
    case 0:
        ret((uint32_t)g_music[g_music_channel].volume);
        break;
    case 1:
        ret((uint32_t)music_status());
        break;
    case 4:
        ret((uint32_t)g_music_channel);
        break;
    case 5:
        ret(AUDIO_CHANNELS);
        break;
    case 6: /* MP3 supported */
    case 9:
        ret(g_device ? 1 : 0);
        break;
    default:
        ret((uint32_t)-1);
        break;
    }
}

IMPORT(s3eAudioSetInt) {
    if (arg(0) == 0) {
        lock();
        g_music[g_music_channel].volume = clamp_volume((int)arg(1));
        unlock();
        ret(0);
    } else if (arg(0) == 4 && arg(1) < AUDIO_CHANNELS) {
        g_music_channel = (int)arg(1);
        ret(0);
    } else {
        ret(1);
    }
}

IMPORT(s3eAudioRegister) {
    if (arg(0) >= S3E_AUDIO_CALLBACK_COUNT || !arg(1)) {
        ret(1);
        return;
    }
    g_audio_callbacks[arg(0)] = (struct guest_callback){arg(1), arg(2)};
    ret(0);
}

IMPORT(s3eSoundGetFreeChannel) {
    if (!g_device) {
        ret((uint32_t)-1);
        return;
    }
    audio_pump();
    for (int c = 0; c < SOUND_CHANNELS; ++c) {
        if (!g_sound[c].pcm) {
            ret((uint32_t)c);
            return;
        }
    }
    ret((uint32_t)-1);
}

IMPORT(s3eSoundSetInt) {
    if (arg(0) == 0) {
        lock();
        g_sound_volume = clamp_volume((int)arg(1));
        unlock();
        ret(0);
    } else if (arg(0) == 2) {
        g_sound_rate = (int)arg(1) > 0 && (int)arg(1) < SOUND_RATE_MAX ? (int)arg(1) : g_sound_rate;
        ret(0);
    } else {
        ret(1);
    }
}

IMPORT(s3eSoundGetInt) {
    switch (arg(0)) {
    case 0:
        ret((uint32_t)g_sound_volume);
        break;
    case 1:
        ret(SOUND_FREQUENCY);
        break;
    case 2:
        ret((uint32_t)g_sound_rate);
        break;
    case 3:
        ret(SOUND_CHANNELS);
        break;
    case 5:
        ret(g_device ? 1 : 0);
        break;
    case 7:
        ret(0);
        break;
    default:
        ret((uint32_t)-1);
        break;
    }
}

static int valid_channel(uint32_t c) {
    return c < SOUND_CHANNELS;
}

IMPORT(s3eSoundChannelRegister) {
    uint32_t c = arg(0), type = arg(1);
    if (!valid_channel(c) || type >= S3E_CHANNEL_CALLBACK_COUNT || type == 3 || !arg(2)) {
        ret(1);
        return;
    }
    g_sound[c].callbacks[type] = (struct guest_callback){arg(2), arg(3)};
    ret(0);
}

IMPORT(s3eSoundChannelUnRegister) {
    uint32_t c = arg(0), type = arg(1);
    if (!valid_channel(c) || type >= S3E_CHANNEL_CALLBACK_COUNT) {
        ret(1);
        return;
    }
    memset(&g_sound[c].callbacks[type], 0, sizeof(g_sound[c].callbacks[type]));
    ret(0);
}

IMPORT(s3eSoundChannelPlay) {
    int32_t c = (int32_t)arg(0);
    uint32_t data = arg(1), samples = arg(2), repeat = arg(3), loop = arg(4);
    if (!g_device || !data || !samples) {
        ret(1);
        return;
    }
    audio_pump();
    if (c < 0) {
        for (c = 0; c < SOUND_CHANNELS && g_sound[c].pcm; ++c) {
        }
    }
    if (!valid_channel((uint32_t)c)) {
        ret(1);
        return;
    }
    ret((uint32_t)start_channel(c, data, samples, repeat, loop, 0));
}

IMPORT(s3eSoundChannelStop) {
    if (!valid_channel(arg(0))) {
        ret(1);
        return;
    }
    stop_channel((int)arg(0), 1);
    ret(0);
}

IMPORT(s3eSoundChannelPause) {
    if (!valid_channel(arg(0))) {
        ret(1);
        return;
    }
    lock();
    g_sound[arg(0)].paused = 1;
    unlock();
    ret(0);
}

IMPORT(s3eSoundChannelResume) {
    if (!valid_channel(arg(0))) {
        ret(1);
        return;
    }
    lock();
    g_sound[arg(0)].paused = 0;
    unlock();
    ret(0);
}

IMPORT(s3eSoundChannelSetInt) {
    uint32_t c = arg(0);
    int32_t value = (int32_t)arg(2);
    if (!valid_channel(c)) {
        ret(1);
        return;
    }
    struct sound_channel *ch = &g_sound[c];
    lock();
    switch (arg(1)) {
    case 0: /* rate scale, 16.16 */
        ch->rate_scale = value;
        ch->rate = (int)(((int64_t)g_sound_rate * (value >> 8) + 0xff) >> 8);
        break;
    case 1:
        ch->rate = value < 0 ? 0 : value > SOUND_RATE_MAX ? SOUND_RATE_MAX : value;
        break;
    case 2:
        ch->user_value = value;
        break;
    case 3:
        ch->volume = clamp_volume(value);
        break;
    default:
        unlock();
        ret(1);
        return;
    }
    unlock();
    ret(0);
}

IMPORT(s3eSoundChannelGetInt) {
    uint32_t c = arg(0);
    if (!valid_channel(c)) {
        ret((uint32_t)-1);
        return;
    }
    struct sound_channel *ch = &g_sound[c];
    switch (arg(1)) {
    case 0:
        ret((uint32_t)ch->rate_scale);
        break;
    case 1:
        ret((uint32_t)ch->rate);
        break;
    case 2:
        ret((uint32_t)ch->user_value);
        break;
    case 3:
        ret((uint32_t)ch->volume);
        break;
    case 4:
        ret(ch->pcm != NULL);
        break;
    case 5:
        ret((uint32_t)ch->paused);
        break;
    default:
        ret((uint32_t)-1);
        break;
    }
}

/* Voice chat (isAudioUnit) is not supported: report success, never deliver audio. */
static void imp_unit_ok(void) {
    ret(0);
}

static void imp_unit_fail(void) {
    ret(1);
}

void audio_unit_fill_interface(uint32_t *table) {
    table[0] = import_thunk("isAudioUnitRegister", imp_unit_ok);
    table[1] = import_thunk("isAudioUnitUnRegister", imp_unit_ok);
    table[2] = import_thunk("isAudioUnitInit", imp_unit_ok);
    table[3] = import_thunk("isAudioUnitTerminate", imp_unit_ok);
    table[4] = import_thunk("isAudioUnitSetCallbacks", imp_unit_ok);
    table[5] = import_thunk("isAudioUnitStart", imp_unit_fail);
    table[6] = import_thunk("isAudioUnitStop", imp_unit_ok);
    table[7] = import_resolve("s3eAudioPlay");
    table[8] = import_resolve("s3eAudioStop");
    table[9] = import_resolve("s3eAudioResume");
    table[10] = import_resolve("s3eAudioPause");
    table[11] = import_resolve("s3eAudioSetInt");
    table[12] = import_resolve("s3eAudioGetInt");
}

void register_audio_imports(void) {
    REG(s3eAudioPlay);
    REG(s3eAudioPlayFromBuffer);
    REG(s3eAudioStop);
    REG(s3eAudioPause);
    REG(s3eAudioResume);
    REG(s3eAudioIsPlaying);
    REG(s3eAudioGetInt);
    REG(s3eAudioSetInt);
    REG(s3eAudioRegister);
    REG(s3eSoundGetFreeChannel);
    REG(s3eSoundSetInt);
    REG(s3eSoundGetInt);
    REG(s3eSoundChannelRegister);
    REG(s3eSoundChannelUnRegister);
    REG(s3eSoundChannelPlay);
    REG(s3eSoundChannelStop);
    REG(s3eSoundChannelPause);
    REG(s3eSoundChannelResume);
    REG(s3eSoundChannelSetInt);
    REG(s3eSoundChannelGetInt);
}
