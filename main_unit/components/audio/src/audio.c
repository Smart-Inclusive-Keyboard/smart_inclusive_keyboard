/*
 * I2S WAV player.
 *
 * Uses the IDF v5 I2S Standard mode driver (the legacy
 * driver/i2s.h API was removed in v5.0). One TX channel is
 * opened on the port / pins from board_get()->i2s; a worker
 * task decodes the RIFF header and streams the PCM payload to
 * the channel in ~4 KB chunks.
 *
 * The "preempting mixer" semantics required by the narrator
 * are implemented by a single playback slot guarded by a
 * mutex: a new audio_play_wav() call atomically swaps the
 * active clip pointer and signals the worker via a binary
 * semaphore. The worker then aborts the previous clip mid-
 * frame and starts the new one from sample 0.
 */

#include "sdkconfig.h"

#if CONFIG_BOARD_HAS_SPEAKER

#include "audio.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <esp_check.h>
#include <esp_log.h>
#include <driver/i2s_std.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"

#include "board.h"

static const char *TAG = "audio";

#define DEFAULT_SAMPLE_RATE 22050

static i2s_chan_handle_t s_tx;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_kick;
static volatile bool      s_playing;
static int                s_volume_pct = 70;
static uint32_t           s_active_sr;        /* current configured rate */
static i2c_master_bus_handle_t s_codec_bus;   /* shared with other I2C users, if any */
static const audio_codec_ctrl_if_t *s_codec_ctrl;
static const audio_codec_gpio_if_t *s_codec_gpio;
static const audio_codec_if_t      *s_codec;  /* es8311 instance, NULL if no codec */

/* Active clip slot. The worker takes a local snapshot of this
 * under the mutex on every wake-up so a midway preemption only
 * has to abort the inner write loop. */
typedef struct {
    const uint8_t *data;
    size_t         len;
    uint32_t       gen;     /* increments on every play -- worker
                             * compares to detect preemption */
} clip_t;

static clip_t s_active;

/* ----- RIFF WAVE header parsing ----- */

typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits;
    const uint8_t *pcm;
    size_t   pcm_len;
} wav_info_t;

static bool parse_wav(const uint8_t *d, size_t n, wav_info_t *info)
{
    if (n < 44) return false;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t i = 12;
    bool got_fmt = false;
    while (i + 8 <= n) {
        const char *id = (const char *)(d + i);
        uint32_t sz = (uint32_t)d[i + 4]
                    | ((uint32_t)d[i + 5] << 8)
                    | ((uint32_t)d[i + 6] << 16)
                    | ((uint32_t)d[i + 7] << 24);
        i += 8;
        if (i + sz > n) return false;
        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            info->format     = d[i] | (d[i + 1] << 8);
            info->channels   = d[i + 2] | (d[i + 3] << 8);
            info->sample_rate = (uint32_t)d[i + 4]
                              | ((uint32_t)d[i + 5] << 8)
                              | ((uint32_t)d[i + 6] << 16)
                              | ((uint32_t)d[i + 7] << 24);
            info->bits = d[i + 14] | (d[i + 15] << 8);
            got_fmt = true;
        } else if (memcmp(id, "data", 4) == 0 && got_fmt) {
            info->pcm = d + i;
            info->pcm_len = sz;
            return info->format == 1 /* PCM */ && info->bits == 16;
        }
        i += sz;
    }
    return false;
}

static void apply_volume(int16_t *samples, size_t n_samples)
{
    /* Codec hardware handles attenuation when present. */
    if (s_codec) return;
    if (s_volume_pct >= 100) return;
    int32_t gain = (s_volume_pct * 256) / 100;
    for (size_t i = 0; i < n_samples; ++i) {
        samples[i] = (int16_t)((samples[i] * gain) >> 8);
    }
}

static float volume_pct_to_db(int pct)
{
    /* Map 1..100% -> -40..0 dB; 0% -> -96 dB (effectively muted). */
    if (pct <= 0) return -96.0f;
    if (pct >= 100) return 0.0f;
    return -40.0f + (40.0f * (float)pct / 100.0f);
}

static esp_err_t reconfigure_clock(uint32_t sample_rate)
{
    if (s_active_sr == sample_rate) return ESP_OK;
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx, &clk);
    if (err != ESP_OK) return err;
    s_active_sr = sample_rate;
    if (s_codec) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 1,
            .channel_mask    = 0,
            .sample_rate     = sample_rate,
            .mclk_multiple   = 0,
        };
        s_codec->set_fs(s_codec, &fs);
    }
    return ESP_OK;
}

static void codec_bringup(const board_t *b)
{
    if (b->codec.i2c_port < 0) return;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_t)b->codec.i2c_port,
        .sda_io_num = b->codec.sda,
        .scl_io_num = b->codec.scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    /* Create the I2C master bus for the codec. On boards where the
     * codec shares its I2C bus with another peripheral, whichever
     * component runs first creates the bus and the other
     * re-acquires it. Try to create it first; if the port is
     * already initialized, fall back to fetching the existing
     * handle. Doing it in this order avoids the noisy "port has
     * not been initialized" error that i2c_master_get_bus_handle()
     * logs when the bus does not exist yet. */
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_codec_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        /* Bus already created by another component -- reuse it. */
        if (i2c_master_get_bus_handle((i2c_port_num_t)b->codec.i2c_port,
                                      &s_codec_bus) != ESP_OK) {
            ESP_LOGE(TAG, "i2c bus already in use but handle fetch failed");
            return;
        }
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus create failed: %s", esp_err_to_name(err));
        return;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (uint8_t)b->codec.i2c_port,
        /* esp_codec_dev expects 8-bit (left-shifted) address; board
         * stores the 7-bit form. */
        .addr = (uint8_t)(b->codec.addr << 1),
        .bus_handle = s_codec_bus,
    };
    s_codec_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_codec_ctrl) {
        ESP_LOGE(TAG, "codec i2c ctrl init failed (addr 0x%02x)",
                 b->codec.addr);
        return;
    }
    s_codec_gpio = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if     = s_codec_ctrl,
        .gpio_if     = s_codec_gpio,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = b->codec.pa_pin,
        .pa_reverted = b->codec.pa_active_low,
        .use_mclk    = (b->i2s.mclk >= 0),
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .no_dac_ref  = false,
        .mclk_div    = 0,
        .master_mode = false,
    };
    s_codec = es8311_codec_new(&es8311_cfg);
    if (!s_codec) {
        ESP_LOGE(TAG, "ES8311 init failed");
        return;
    }
    s_codec->enable(s_codec, true);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 1,
        .channel_mask    = 0,
        .sample_rate     = DEFAULT_SAMPLE_RATE,
        .mclk_multiple   = 0,
    };
    s_codec->set_fs(s_codec, &fs);
    s_active_sr = DEFAULT_SAMPLE_RATE;
    s_codec->set_vol(s_codec, volume_pct_to_db(s_volume_pct));
    ESP_LOGI(TAG, "ES8311 ready (addr 0x%02x port %d)",
             b->codec.addr, b->codec.i2c_port);
}

static void worker(void *arg)
{
    (void)arg;
    /* 4 KB DMA-friendly chunk in internal RAM. */
    int16_t *buf = malloc(4096);
    if (!buf) {
        ESP_LOGE(TAG, "OOM allocating play buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        xSemaphoreTake(s_kick, portMAX_DELAY);

        clip_t snap;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        snap = s_active;
        xSemaphoreGive(s_mutex);

        if (!snap.data) {
            s_playing = false;
            continue;
        }

        wav_info_t w = { 0 };
        if (!parse_wav(snap.data, snap.len, &w)) {
            ESP_LOGW(TAG, "WAV header not understood (%u bytes)",
                     (unsigned)snap.len);
            s_playing = false;
            continue;
        }
        reconfigure_clock(w.sample_rate);

        s_playing = true;

        size_t offset = 0;
        while (offset < w.pcm_len) {
            /* Check for preemption: a newer generation means the
             * caller has handed us a new clip. */
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool preempted = (s_active.gen != snap.gen);
            xSemaphoreGive(s_mutex);
            if (preempted) break;

            size_t chunk = w.pcm_len - offset;
            if (chunk > 4096) chunk = 4096;
            memcpy(buf, w.pcm + offset, chunk);
            apply_volume(buf, chunk / 2);

            size_t written = 0;
            i2s_channel_write(s_tx, buf, chunk, &written,
                              pdMS_TO_TICKS(100));
            offset += written ? written : chunk;
        }

        /* The I2S DMA descriptors are circular: once the worker
         * stops feeding it, the controller keeps replaying
         * whatever PCM is still sitting in the DMA buffers,
         * which sounds like the final note looping forever.
         * Flush the pipeline with zeroed samples so playback
         * ends in silence. The DMA holds dma_desc_num *
         * dma_frame_num * 2 bytes (= 2880 here); two 4 KB
         * silence writes comfortably overwrite it. */
        memset(buf, 0, 4096);
        for (int k = 0; k < 2; ++k) {
            size_t written = 0;
            i2s_channel_write(s_tx, buf, 4096, &written,
                              pdMS_TO_TICKS(100));
        }
        s_playing = false;
    }
}

int audio_init(void)
{
    if (s_tx) return 0;

    const board_t *b = board_get();
    if (b->i2s.bclk < 0) {
        ESP_LOGW(TAG, "No I2S pins configured");
        return -1;
    }

    s_mutex = xSemaphoreCreateMutex();
    s_kick  = xSemaphoreCreateBinary();
    if (!s_mutex || !s_kick) return -1;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        b->i2s.port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = (b->i2s.mclk < 0) ? I2S_GPIO_UNUSED : b->i2s.mclk,
            .bclk = b->i2s.bclk,
            .ws   = b->i2s.lrck,
            .dout = b->i2s.dout,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    s_active_sr = DEFAULT_SAMPLE_RATE;
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    codec_bringup(b);

    xTaskCreatePinnedToCore(worker, "audio", 4096, NULL, 6, NULL, 1);

    ESP_LOGI(TAG, "I2S up on port %d (MCLK=%d BCLK=%d LRCK=%d DOUT=%d)",
             b->i2s.port, b->i2s.mclk, b->i2s.bclk, b->i2s.lrck, b->i2s.dout);
    return 0;
}

int audio_play_wav(const void *data, size_t len)
{
    if (s_volume_pct == 0) return 0;
    if (!s_tx) return -1;
    if (!data || len == 0) return -1;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active.data = data;
    s_active.len  = len;
    s_active.gen++;
    xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_kick);
    return 0;
}

void audio_stop(void)
{
    if (s_volume_pct == 0) return;
    if (!s_tx) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_active.data = NULL;
    s_active.len  = 0;
    s_active.gen++;
    xSemaphoreGive(s_mutex);
}

void audio_set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume_pct = percent;
    if (s_codec) {
        s_codec->set_vol(s_codec, volume_pct_to_db(percent));
    }
}

int audio_get_volume(void)
{
    return s_volume_pct;
}

bool audio_is_playing(void) { return s_playing; }

/* ----- Startup tune ------------------------------------------------
 *
 * Generates a tiny RIFF/WAVE blob in heap memory: a three-note
 * ascending arpeggio (C5 -> E5 -> G5) at 22050 Hz, 16-bit mono,
 * ~110 ms per note. Each note fades in and out with a short
 * linear envelope so it doesn't click. The buffer is allocated
 * once and never freed -- audio_play_wav() retains the pointer
 * for the duration of playback, and this clip is short and
 * one-shot, so leaking it on success is intentional.
 */
#define TUNE_SAMPLE_RATE 22050
#define TUNE_NOTE_MS     110
#define TUNE_NOTES       3
#define TUNE_AMPLITUDE   12000  /* int16 peak, leaves headroom    */
#define TUNE_FADE_MS     8

void audio_play_startup_tune(void)
{
    if (audio_init() != 0) return;

    static const float NOTE_HZ[TUNE_NOTES] = {
        523.25f,  /* C5 */
        659.25f,  /* E5 */
        783.99f,  /* G5 */
    };

    const size_t note_samples = (TUNE_SAMPLE_RATE * TUNE_NOTE_MS) / 1000;
    const size_t fade_samples = (TUNE_SAMPLE_RATE * TUNE_FADE_MS) / 1000;
    const size_t total_samples = note_samples * TUNE_NOTES;
    const size_t pcm_bytes = total_samples * sizeof(int16_t);
    const size_t blob_bytes = 44 + pcm_bytes;

    uint8_t *blob = malloc(blob_bytes);
    if (!blob) {
        ESP_LOGW(TAG, "startup tune: OOM (%u bytes)",
                 (unsigned)blob_bytes);
        return;
    }

    /* RIFF/WAVE header (PCM, 16-bit, mono). */
    uint32_t riff_size = (uint32_t)(blob_bytes - 8);
    uint32_t data_size = (uint32_t)pcm_bytes;
    uint32_t byte_rate = TUNE_SAMPLE_RATE * 2;
    memcpy(blob + 0,  "RIFF", 4);
    blob[4] = (uint8_t)(riff_size);
    blob[5] = (uint8_t)(riff_size >> 8);
    blob[6] = (uint8_t)(riff_size >> 16);
    blob[7] = (uint8_t)(riff_size >> 24);
    memcpy(blob + 8,  "WAVE", 4);
    memcpy(blob + 12, "fmt ", 4);
    blob[16] = 16; blob[17] = 0; blob[18] = 0; blob[19] = 0; /* fmt size */
    blob[20] = 1;  blob[21] = 0;                              /* PCM */
    blob[22] = 1;  blob[23] = 0;                              /* mono */
    blob[24] = (uint8_t)(TUNE_SAMPLE_RATE);
    blob[25] = (uint8_t)(TUNE_SAMPLE_RATE >> 8);
    blob[26] = (uint8_t)(TUNE_SAMPLE_RATE >> 16);
    blob[27] = (uint8_t)(TUNE_SAMPLE_RATE >> 24);
    blob[28] = (uint8_t)(byte_rate);
    blob[29] = (uint8_t)(byte_rate >> 8);
    blob[30] = (uint8_t)(byte_rate >> 16);
    blob[31] = (uint8_t)(byte_rate >> 24);
    blob[32] = 2;  blob[33] = 0;                              /* block align */
    blob[34] = 16; blob[35] = 0;                              /* bits */
    memcpy(blob + 36, "data", 4);
    blob[40] = (uint8_t)(data_size);
    blob[41] = (uint8_t)(data_size >> 8);
    blob[42] = (uint8_t)(data_size >> 16);
    blob[43] = (uint8_t)(data_size >> 24);

    int16_t *pcm = (int16_t *)(blob + 44);
    const float two_pi = 6.28318530718f;
    for (int n = 0; n < TUNE_NOTES; ++n) {
        float phase_step = two_pi * NOTE_HZ[n] / (float)TUNE_SAMPLE_RATE;
        float phase = 0.0f;
        for (size_t i = 0; i < note_samples; ++i) {
            float env = 1.0f;
            if (i < fade_samples) {
                env = (float)i / (float)fade_samples;
            } else if (i > note_samples - fade_samples) {
                env = (float)(note_samples - i) / (float)fade_samples;
            }
            float s = sinf(phase) * env * (float)TUNE_AMPLITUDE;
            phase += phase_step;
            if (phase >= two_pi) phase -= two_pi;
            pcm[n * note_samples + i] = (int16_t)s;
        }
    }

    audio_play_wav(blob, blob_bytes);
}

#endif /* SPEAKER */
