#include "phm_radio.h"

#include "phm_gen.h"
#include "phm_privacy.h"

#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_worker.h>

#include <string.h>

#define TAG "Pheme"

#define PHM_SCAN_DWELL_MS   900
#define PHM_SCAN_SETTLE_MS  3
#define PHM_SCAN_SAMPLES    12
#define PHM_SCAN_SAMPLE_US  600
#define PHM_DEMO_GAP_MS     1700

/*
 * Every one of these is inside a CC1101 band, and every one of them is a place
 * paging actually happens. The two labels matter as much as the number: a user
 * who tunes 454.475 and hears nothing should know they were listening to a US
 * commercial paging channel and not to a mistake.
 */
const PhmChannel phm_channels[PHM_CHANNEL_COUNT] = {
    {.frequency = 433920000, .label = "433.92", .use = "On-site ISM"},
    {.frequency = 439987500, .label = "439.99", .use = "DAPNET ham"},
    {.frequency = 448425000, .label = "448.42", .use = "Commercial"},
    {.frequency = 454025000, .label = "454.02", .use = "US paging"},
    {.frequency = 454475000, .label = "454.47", .use = "US paging"},
    {.frequency = 454650000, .label = "454.65", .use = "US paging"},
    {.frequency = 462850000, .label = "462.85", .use = "US on-site"},
    {.frequency = 868350000, .label = "868.35", .use = "On-site EU"},
};

const PhmBlindSpot phm_blind_spots[PHM_BLIND_COUNT] = {
    {.label = "153 MHz", .use = "UK national", .why = "VHF, far below the CC1101"},
    {.label = "169.65", .use = "NL P2000", .why = "VHF, and FLEX not POCSAG"},
    {.label = "466 MHz", .use = "EU commercial", .why = "2 MHz above the chip's edge"},
    {.label = "929-932", .use = "US national", .why = "above the 928 MHz edge"},
};

/*
 * A tighter receive filter than the stock 2FSK preset, offered as a setting.
 *
 * POCSAG deviates by about 4.5 kHz and runs at 1200 bits per second, so the
 * whole signal fits inside eleven kilohertz. The stock preset opens the filter
 * far wider than that, which costs sensitivity and lets neighbouring traffic in.
 * 58 kHz is as narrow as a CC1101 with a 26 MHz crystal can go, and it leaves
 * ample room for both ends of the link to be off frequency.
 *
 * It is a setting rather than the default because it has not been confirmed
 * against a live paging transmitter, and a preset that does not work is much
 * worse than one that merely works less well. The stock preset is the one the
 * app starts with.
 *
 * Register pairs, terminated by a zero register, then the eight-byte PA table.
 */
static const uint8_t phm_preset_narrow_1200[] = {
    0x02, 0x0D, /* IOCFG0    GDO0 = asynchronous serial data out          */
    0x03, 0x07, /* FIFOTHR                                                */
    0x08, 0x32, /* PKTCTRL0  asynchronous serial mode, infinite length    */
    0x0B, 0x06, /* FSCTRL1                                                */
    0x10, 0xF5, /* MDMCFG4   filter 58 kHz, data rate exponent 5          */
    0x11, 0x83, /* MDMCFG3   data rate mantissa: 1199.6 bit/s             */
    0x12, 0x00, /* MDMCFG2   2-FSK, no Manchester, no sync word           */
    0x13, 0x22, /* MDMCFG1                                                */
    0x14, 0xF8, /* MDMCFG0                                                */
    0x15, 0x14, /* DEVIATN   4.76 kHz                                     */
    0x18, 0x18, /* MCSM0     calibrate on IDLE -> RX                      */
    0x19, 0x16, /* FOCCFG                                                 */
    0x1B, 0x43, /* AGCCTRL2                                               */
    0x1C, 0x49, /* AGCCTRL1                                               */
    0x1D, 0x91, /* AGCCTRL0                                               */
    0x20, 0xFB, /* WORCTRL                                                */
    0x21, 0x56, /* FREND1                                                 */
    0x22, 0x10, /* FREND0                                                 */
    0x23, 0xE9, /* FSCAL3                                                 */
    0x24, 0x2A, /* FSCAL2                                                 */
    0x25, 0x00, /* FSCAL1                                                 */
    0x26, 0x1F, /* FSCAL0                                                 */
    0x2C, 0x81, /* TEST2                                                  */
    0x2D, 0x35, /* TEST1                                                  */
    0x2E, 0x09, /* TEST0                                                  */
    0x00, 0x00, /* end of registers                                       */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PA table, RX only  */
};

typedef enum {
    PhmModeIdle,
    PhmModeListen,
    PhmModeDemo,
    PhmModeScan,
} PhmMode;

struct PhmRadio {
    ViewDispatcher* view_dispatcher;
    uint32_t page_event;

    SubGhzWorker* worker;
    const SubGhzDevice* device;
    PhmPocsag* pocsag;

    FuriMutex* mutex; /* guards the session          */
    FuriMutex* dev_mutex; /* serialises SPI to the CC1101 */

    volatile uint8_t mode;
    uint8_t channel_idx;
    int8_t baud_lock;
    bool narrow;

    uint32_t started_tick;

    PhmSpool spool;
    PhmRoster roster;

    FuriThread* thread;
    volatile bool thread_running;

    /* The HAL's stop_async_rx is a furi_check on the radio already being in
     * async RX, so calling it speculatively is a crash rather than a no-op.
     * The scan thread stops before it starts on its very first channel, so
     * this flag is what keeps that legal. */
    bool rx_active;

    PhmScanBand scan[PHM_CHANNEL_COUNT];
    uint32_t scan_sweeps;
};

/* ---------------------------------------------------------------- pages ---- */

static int8_t phm_dbm_clamp(float rssi) {
    if(rssi > 0.0f) return 0;
    if(rssi < -127.0f) return -127;
    return (int8_t)rssi;
}

static float phm_read_rssi(const PhmRadio* radio) {
    PhmRadio* self = (PhmRadio*)radio;
    float rssi = -127.0f;
    furi_mutex_acquire(self->dev_mutex, FuriWaitForever);
    if(self->device) rssi = subghz_devices_get_rssi(self->device);
    furi_mutex_release(self->dev_mutex);
    return rssi;
}

/*
 * A page came out of the decoder. Runs on the Sub-GHz worker thread during a
 * live capture and on the demo thread otherwise, so everything it touches is
 * behind the session mutex and nothing large lands on the stack.
 */
static void phm_on_page(void* context, const PhmPage* page) {
    PhmRadio* radio = context;

    int8_t rssi = (radio->mode == PhmModeDemo) ? -68 : phm_dbm_clamp(phm_read_rssi(radio));

    furi_mutex_acquire(radio->mutex, FuriWaitForever);

    /* One record, built in place in the ring rather than on the stack: this
     * runs on the Sub-GHz worker, which has two kilobytes to its name and a
     * PhmRecord is a quarter of that. */
    PhmRecord* slot = &radio->spool.item[radio->spool.head];
    memset(slot, 0, sizeof(*slot));
    slot->page = *page;
    slot->tick_ms = furi_get_tick();
    slot->frequency = phm_channels[radio->channel_idx].frequency;
    slot->baud_idx = page->baud_idx;
    slot->rssi = rssi;
    phm_privacy_classify(page->text, page->len, &slot->exposure);

    PhmRecord* record = slot;
    radio->spool.head = (uint8_t)((radio->spool.head + 1u) % PHM_SPOOL_MAX);
    if(radio->spool.count < PHM_SPOOL_MAX) radio->spool.count++;
    if(radio->spool.total < 0xFFFFu) radio->spool.total++;
    if(!radio->spool.have_worst || record->exposure.score > radio->spool.worst.exposure.score) {
        radio->spool.worst = *record;
        radio->spool.have_worst = true;
    }

    if(radio->mode == PhmModeScan) {
        for(uint8_t i = 0; i < PHM_CHANNEL_COUNT; i++) {
            if(phm_channels[i].frequency == record->frequency && radio->scan[i].pages < 0xFFFFu) {
                radio->scan[i].pages++;
                break;
            }
        }
    }

    phm_roster_add(&radio->roster, record);

    furi_mutex_release(radio->mutex);

    if(radio->view_dispatcher) {
        view_dispatcher_send_custom_event(radio->view_dispatcher, radio->page_event);
    }
}

/* Worker pair callback: one level and how long it lasted. */
static void phm_on_pair(void* context, bool level, uint32_t duration) {
    PhmRadio* radio = context;
    phm_pocsag_feed_pair(radio->pocsag, level, duration);
}

static void phm_on_overrun(void* context) {
    PhmRadio* radio = context;
    phm_pocsag_reset(radio->pocsag);
}

/* ---------------------------------------------------------------- device ---- */

static void phm_device_up(PhmRadio* radio, uint32_t frequency) {
    furi_hal_power_suppress_charge_enter(); /* the charger is a noise source */

    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    subghz_devices_begin(radio->device);
    subghz_devices_reset(radio->device);

    if(radio->narrow) {
        subghz_devices_load_preset(
            radio->device, FuriHalSubGhzPresetCustom, (uint8_t*)phm_preset_narrow_1200);
    } else {
        subghz_devices_load_preset(radio->device, FuriHalSubGhzPreset2FSKDev476Async, NULL);
    }

    if(!subghz_devices_is_frequency_valid(radio->device, frequency)) {
        frequency = phm_channels[PHM_CHANNEL_DEFAULT].frequency;
    }
    subghz_devices_set_frequency(radio->device, frequency);
}

static void phm_device_down(PhmRadio* radio) {
    furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
    radio->rx_active = false;
    subghz_devices_idle(radio->device);
    subghz_devices_sleep(radio->device);
    subghz_devices_end(radio->device);
    subghz_devices_deinit();
    radio->device = NULL;
    furi_mutex_release(radio->dev_mutex);

    furi_hal_power_suppress_charge_exit();
}

static void phm_rx_start(PhmRadio* radio) {
    furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
    if(radio->device && !radio->rx_active) {
        subghz_devices_start_async_rx(
            radio->device, (void*)subghz_worker_rx_callback, radio->worker);
        radio->rx_active = true;
    }
    furi_mutex_release(radio->dev_mutex);
}

/* Takes the same lock an RSSI read does, because the worker can be inside the
 * decoder - and so inside a read - at the moment RX is torn down. */
static void phm_rx_stop(PhmRadio* radio) {
    furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
    if(radio->device && radio->rx_active) {
        subghz_devices_stop_async_rx(radio->device);
        radio->rx_active = false;
    }
    furi_mutex_release(radio->dev_mutex);
}

/* ------------------------------------------------------------- lifecycle ---- */

PhmRadio* phm_radio_alloc(ViewDispatcher* view_dispatcher, uint32_t page_event) {
    PhmRadio* radio = malloc(sizeof(PhmRadio));
    memset(radio, 0, sizeof(PhmRadio));

    radio->view_dispatcher = view_dispatcher;
    radio->page_event = page_event;
    radio->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    radio->dev_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    radio->channel_idx = PHM_CHANNEL_DEFAULT;
    radio->baud_lock = -1;
    radio->mode = PhmModeIdle;

    radio->pocsag = phm_pocsag_alloc();
    phm_pocsag_set_callback(radio->pocsag, phm_on_page, radio);

    radio->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(radio->worker, phm_on_overrun);
    subghz_worker_set_pair_callback(radio->worker, phm_on_pair);
    subghz_worker_set_context(radio->worker, radio);

    phm_spool_reset(&radio->spool);
    phm_roster_reset(&radio->roster);

    return radio;
}

void phm_radio_free(PhmRadio* radio) {
    furi_assert(radio);
    phm_radio_stop_all(radio);
    subghz_worker_free(radio->worker);
    phm_pocsag_free(radio->pocsag);
    furi_mutex_free(radio->dev_mutex);
    furi_mutex_free(radio->mutex);
    free(radio);
}

void phm_radio_configure(PhmRadio* radio, uint8_t channel_idx, int8_t baud_lock, bool narrow) {
    furi_assert(radio);
    if(channel_idx >= PHM_CHANNEL_COUNT) channel_idx = PHM_CHANNEL_DEFAULT;

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    radio->channel_idx = channel_idx;
    radio->baud_lock = baud_lock;
    radio->narrow = narrow;
    furi_mutex_release(radio->mutex);

    phm_pocsag_set_lane(radio->pocsag, baud_lock);
}

void phm_radio_reset_session(PhmRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    phm_spool_reset(&radio->spool);
    phm_roster_reset(&radio->roster);
    memset(radio->scan, 0, sizeof(radio->scan));
    radio->scan_sweeps = 0;
    radio->started_tick = furi_get_tick();
    furi_mutex_release(radio->mutex);

    phm_pocsag_reset(radio->pocsag);
}

/* ---------------------------------------------------------------- listen ---- */

void phm_radio_listen_start(PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode != PhmModeIdle) return;

    /* A session's clock starts when the session does, not when the radio is
     * re-armed after a detour into the page list. */
    if(radio->started_tick == 0) radio->started_tick = furi_get_tick();
    phm_pocsag_reset(radio->pocsag);

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    uint32_t frequency = phm_channels[radio->channel_idx].frequency;
    furi_mutex_release(radio->mutex);

    phm_device_up(radio, frequency);

    subghz_worker_start(radio->worker);
    phm_rx_start(radio);

    radio->mode = PhmModeListen;
}

void phm_radio_listen_stop(PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode != PhmModeListen) return;
    radio->mode = PhmModeIdle;

    phm_rx_stop(radio);
    subghz_worker_stop(radio->worker);
    phm_device_down(radio);

    phm_pocsag_flush(radio->pocsag);
}

bool phm_radio_is_listening(const PhmRadio* radio) {
    return radio && radio->mode == PhmModeListen;
}

/* ------------------------------------------------------------------ demo ---- */

static void phm_demo_emit(void* context, bool level, uint32_t duration_us) {
    PhmRadio* radio = context;
    if(!radio->thread_running) return;
    phm_pocsag_feed_pair(radio->pocsag, level, duration_us);
}

/*
 * Demo mode exists because almost nobody has a paging transmitter to hand, and
 * an app whose central screen only ever says "listening" teaches nothing. The
 * generated channel is not injected into the UI: it is modulated into run
 * lengths, fed through the same decoder, corrected by the same BCH tables and
 * graded by the same engine. What appears on screen has genuinely been decoded.
 */
static int32_t phm_demo_thread(void* context) {
    PhmRadio* radio = context;

    uint8_t count = 0;
    const PhmGenPage* demo = phm_gen_demo_pages(&count);
    uint8_t next = 0;
    uint32_t pass = 0;

    while(radio->thread_running) {
        /* One page per beat, so they arrive at a readable pace. Every fourth
         * pass is scattered with bit errors, to show the error correction
         * working and, when it cannot, saying so. */
        uint16_t damage = ((pass % 4u) == 3u) ? 45u : 0u;
        phm_gen_transmission(&demo[next], 1, 1, damage, pass, phm_demo_emit, radio);

        if(++next >= count) {
            next = 0;
            pass++;
        }

        for(uint16_t waited = 0; waited < PHM_DEMO_GAP_MS && radio->thread_running; waited += 50) {
            furi_delay_ms(50);
        }
    }

    return 0;
}

void phm_radio_demo_start(PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode != PhmModeIdle) return;

    if(radio->started_tick == 0) radio->started_tick = furi_get_tick();
    phm_pocsag_reset(radio->pocsag);

    radio->mode = PhmModeDemo;
    radio->thread_running = true;
    radio->thread = furi_thread_alloc_ex("PhmDemo", 2048, phm_demo_thread, radio);
    furi_thread_start(radio->thread);
}

void phm_radio_demo_stop(PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode != PhmModeDemo) return;

    radio->thread_running = false;
    furi_thread_join(radio->thread);
    furi_thread_free(radio->thread);
    radio->thread = NULL;
    radio->mode = PhmModeIdle;
}

bool phm_radio_is_demo(const PhmRadio* radio) {
    return radio && radio->mode == PhmModeDemo;
}

/* ------------------------------------------------------------------ scan ---- */

/*
 * The scan looks for POCSAG framing, not for energy.
 *
 * A signal-strength sweep finds the loudest channel, which on any real site is
 * a data link or a repeater and never the pager base station. Dwelling with the
 * decoder running and counting how many batches synchronised answers the
 * question actually being asked - which of these channels is carrying paging -
 * and it cannot be fooled by a strong carrier that is not POCSAG at all.
 */
static int32_t phm_scan_thread(void* context) {
    PhmRadio* radio = context;

    while(radio->thread_running) {
        for(uint8_t i = 0; i < PHM_CHANNEL_COUNT && radio->thread_running; i++) {
            if(!radio->device) break;

            phm_rx_stop(radio);

            furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
            subghz_devices_idle(radio->device);
            subghz_devices_set_frequency(radio->device, phm_channels[i].frequency);
            furi_mutex_release(radio->dev_mutex);

            /* Each channel is judged on what it carried during its own dwell,
             * so the decoder starts each one from nothing. */
            phm_pocsag_reset(radio->pocsag);

            furi_mutex_acquire(radio->mutex, FuriWaitForever);
            radio->channel_idx = i;
            furi_mutex_release(radio->mutex);

            phm_rx_start(radio);
            furi_delay_ms(PHM_SCAN_SETTLE_MS);

            /* Peak-hold across the dwell: a paging transmitter is silent most
             * of the time, so the maximum is what matters, not the mean. */
            float peak = -127.0f;
            for(uint16_t waited = 0; waited < PHM_SCAN_DWELL_MS && radio->thread_running;
                waited += 30) {
                float sample = phm_read_rssi(radio);
                if(sample > peak) peak = sample;
                furi_delay_ms(30);
            }

            PhmPocsagStatus status;
            phm_pocsag_status(radio->pocsag, &status);

            furi_mutex_acquire(radio->mutex, FuriWaitForever);
            PhmScanBand* band = &radio->scan[i];
            band->frequency = phm_channels[i].frequency;
            band->seen = true;
            int8_t dbm = phm_dbm_clamp(peak);
            if(!band->peak_dbm || dbm > band->peak_dbm) band->peak_dbm = dbm;
            if(status.batches > 0 && band->batches < 0xFFFFu) {
                uint32_t total = band->batches + status.batches;
                band->batches = (total > 0xFFFFu) ? 0xFFFFu : (uint16_t)total;
            }
            furi_mutex_release(radio->mutex);
        }

        furi_mutex_acquire(radio->mutex, FuriWaitForever);
        radio->scan_sweeps++;
        furi_mutex_release(radio->mutex);
    }

    return 0;
}

void phm_radio_scan_start(PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode != PhmModeIdle) return;

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    memset(radio->scan, 0, sizeof(radio->scan));
    radio->scan_sweeps = 0;
    furi_mutex_release(radio->mutex);

    radio->started_tick = furi_get_tick();
    phm_device_up(radio, phm_channels[0].frequency);
    subghz_worker_start(radio->worker);

    radio->mode = PhmModeScan;
    radio->thread_running = true;
    radio->thread = furi_thread_alloc_ex("PhmScan", 1536, phm_scan_thread, radio);
    furi_thread_start(radio->thread);
}

void phm_radio_scan_stop(PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode != PhmModeScan) return;

    radio->thread_running = false;
    furi_thread_join(radio->thread);
    furi_thread_free(radio->thread);
    radio->thread = NULL;

    phm_rx_stop(radio);
    subghz_worker_stop(radio->worker);
    phm_device_down(radio);
    radio->mode = PhmModeIdle;
}

bool phm_radio_is_scanning(const PhmRadio* radio) {
    return radio && radio->mode == PhmModeScan;
}

uint8_t phm_radio_scan_snapshot(const PhmRadio* radio, PhmScanBand* out, uint8_t max) {
    furi_assert(radio);
    if(!out) return 0;

    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    uint8_t n = (PHM_CHANNEL_COUNT < max) ? PHM_CHANNEL_COUNT : max;
    for(uint8_t i = 0; i < n; i++) out[i] = self->scan[i];
    furi_mutex_release(self->mutex);
    return n;
}

uint32_t phm_radio_scan_sweeps(const PhmRadio* radio) {
    furi_assert(radio);
    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    uint32_t sweeps = self->scan_sweeps;
    furi_mutex_release(self->mutex);
    return sweeps;
}

int8_t phm_radio_scan_best(const PhmRadio* radio) {
    furi_assert(radio);
    PhmRadio* self = (PhmRadio*)radio;

    int8_t best = -1;
    uint16_t best_batches = 0;

    furi_mutex_acquire(self->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < PHM_CHANNEL_COUNT; i++) {
        if(self->scan[i].batches > best_batches) {
            best_batches = self->scan[i].batches;
            best = (int8_t)i;
        }
    }
    furi_mutex_release(self->mutex);

    /* No answer is a legitimate answer. A channel that never synchronised is
     * not "the quietest one", it is a channel with no paging on it. */
    return best;
}

void phm_radio_stop_all(PhmRadio* radio) {
    furi_assert(radio);
    switch(radio->mode) {
    case PhmModeListen:
        phm_radio_listen_stop(radio);
        break;
    case PhmModeDemo:
        phm_radio_demo_stop(radio);
        break;
    case PhmModeScan:
        phm_radio_scan_stop(radio);
        break;
    default:
        break;
    }
}

/* ----------------------------------------------------------- accessors ---- */

void phm_radio_status(const PhmRadio* radio, PhmPocsagStatus* out) {
    furi_assert(radio);
    phm_pocsag_status(radio->pocsag, out);
}

float phm_radio_rssi(const PhmRadio* radio) {
    furi_assert(radio);
    if(radio->mode == PhmModeDemo) return -68.0f;
    if(radio->mode == PhmModeIdle) return -127.0f;
    return phm_read_rssi(radio);
}

uint32_t phm_radio_frequency(const PhmRadio* radio) {
    furi_assert(radio);
    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    uint32_t frequency = phm_channels[self->channel_idx].frequency;
    furi_mutex_release(self->mutex);
    return frequency;
}

uint32_t phm_radio_elapsed_ms(const PhmRadio* radio) {
    furi_assert(radio);
    if(radio->started_tick == 0) return 0;
    return furi_get_tick() - radio->started_tick;
}

bool phm_radio_page_at(const PhmRadio* radio, uint8_t index, PhmRecord* out) {
    furi_assert(radio);
    if(!out) return false;

    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    const PhmRecord* record = phm_spool_at(&self->spool, index);
    if(record) *out = *record;
    furi_mutex_release(self->mutex);
    return record != NULL;
}

uint8_t phm_radio_page_count(const PhmRadio* radio) {
    furi_assert(radio);
    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    uint8_t count = self->spool.count;
    furi_mutex_release(self->mutex);
    return count;
}

bool phm_radio_worst_page(const PhmRadio* radio, PhmRecord* out) {
    furi_assert(radio);
    if(!out) return false;

    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    bool have = self->spool.have_worst;
    if(have) *out = self->spool.worst;
    furi_mutex_release(self->mutex);
    return have;
}

uint8_t phm_radio_roster_snapshot(const PhmRadio* radio, PhmPager* out, uint8_t max) {
    furi_assert(radio);
    if(!out) return 0;

    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    phm_roster_sort(&self->roster);
    uint8_t n = (self->roster.count < max) ? self->roster.count : max;
    for(uint8_t i = 0; i < n; i++) out[i] = self->roster.item[i];
    furi_mutex_release(self->mutex);
    return n;
}

uint16_t phm_radio_roster_overflow(const PhmRadio* radio) {
    furi_assert(radio);
    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    uint16_t overflow = self->roster.overflow;
    furi_mutex_release(self->mutex);
    return overflow;
}

void phm_radio_tally(const PhmRadio* radio, PhmTally* out) {
    furi_assert(radio);
    if(!out) return;

    PhmRadio* self = (PhmRadio*)radio;
    furi_mutex_acquire(self->mutex, FuriWaitForever);
    phm_tally(&self->roster, &self->spool, out);
    furi_mutex_release(self->mutex);
}
