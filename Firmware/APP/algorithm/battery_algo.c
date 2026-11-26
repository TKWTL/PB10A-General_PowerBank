/* PB02B discharge-session accounting adapted to integer mWh and retained SRAM.
 * No independent task, I2C operation or floating-point arithmetic. */
#include "battery_algo.h"
#include "mini_crc.h"
#include "main.h"

#define BAT_FULL_MIN_MWH 500u
#define BAT_FULL_MAX_MWH 200000u
#define BAT_RECORD_TAG 0xBA01u
#define BAT_START_SAMPLES 2u
#define BAT_STOP_SAMPLES 4u

typedef struct {
    uint32_t sequence;
    uint32_t full_mwh;
    uint32_t efc_q10000;
    uint32_t tag_crc;
} battery_noinit_t;

static volatile battery_noinit_t s_record[2]
    __attribute__((section(".bss.noinit"), used, aligned(4)));
static uint32_t s_sequence, s_full_mwh, s_efc;
static uint8_t s_slot;
static sw6306_learn_state_t s_learn;
static uint8_t s_done_seen, s_rearm_pending;

typedef struct {
    uint32_t start_mwh, full_mwh, last_mwh;
    uint8_t active, start_count, stop_count;
} battery_session_t;

static battery_session_t s_session;

static uint8_t full_valid(uint32_t full) {
    return full >= BAT_FULL_MIN_MWH && full <= BAT_FULL_MAX_MWH;
}

/* Same CRC16-CCITT (little-endian 3 words, initial 0xffff) as SC7A20. */
static uint8_t record_valid(const battery_noinit_t *r) {
    return (r->tag_crc >> 16) == BAT_RECORD_TAG &&
           full_valid(r->full_mwh) &&
           (uint16_t)r->tag_crc == crc16_ccitt_words3(r->sequence, r->full_mwh, r->efc_q10000);
}

static void record_save(void) {
    if (!full_valid(s_full_mwh))
        return;
    uint8_t next = s_slot ^ 1u;
    uint32_t seq = s_sequence + 1u;
    /* Invalidate only the non-current slot. The old slot remains
     * recoverable if a reset occurs midway through this commit. */
    s_record[next].tag_crc = 0u;
    __DMB();
    s_record[next].sequence = seq;
    s_record[next].full_mwh = s_full_mwh;
    s_record[next].efc_q10000 = s_efc;
    __DMB();
    s_record[next].tag_crc = ((uint32_t)BAT_RECORD_TAG << 16) |
                             crc16_ccitt_words3(seq, s_full_mwh, s_efc);
    __DSB();
    s_slot = next;
    s_sequence = seq;
}

static void session_reset(void) {
    s_session = (battery_session_t){0};
}

void BatteryAlgo_Init(void) {
    battery_noinit_t a = s_record[0], b = s_record[1];
    uint8_t va = record_valid(&a), vb = record_valid(&b);
    uint32_t difference = b.sequence - a.sequence;
    s_slot = vb && (!va || (difference && difference < 0x80000000u)) ? 1u : 0u;
    const battery_noinit_t *r = s_slot ? &b : &a;
    if (va || vb) {
        s_sequence = r->sequence;
        s_full_mwh = r->full_mwh;
        s_efc = r->efc_q10000;
    } else {
        s_sequence = s_full_mwh = s_efc = 0u;
    }
    s_learn = SW6306_LEARN_ST_UNKNOWN;
    s_done_seen = s_rearm_pending = 0u;
    session_reset();
}

static uint32_t session_delta(void) {
    if (!s_session.active || !full_valid(s_session.full_mwh) ||
        s_session.last_mwh >= s_session.start_mwh)
        return 0u;
    uint32_t delta = s_session.start_mwh - s_session.last_mwh;
    if (delta > s_session.full_mwh + s_session.full_mwh / 4u)
        return 0u;
    /* max 250000 * 10000 + 100000 < UINT32_MAX */
    return (delta * BAT_EFC_SCALE + s_session.full_mwh / 2u) / s_session.full_mwh;
}

static uint32_t efc_add(uint32_t delta) {
    return delta > UINT32_MAX - s_efc ? UINT32_MAX : s_efc + delta;
}

void BatteryAlgo_Finalize(void) {
    uint32_t updated = efc_add(session_delta());
    session_reset();
    if (updated != s_efc) {
        s_efc = updated;
        record_save();
    }
}

void BatteryAlgo_InvalidSample(void) {
    s_learn = SW6306_LEARN_ST_UNKNOWN;
    s_session.stop_count = 0u;
    if (!s_session.active)
        session_reset();
}

void BatteryAlgo_Invalidate(void) {
    BatteryAlgo_InvalidSample();
    session_reset();
    /* Retain an observed DONE/rearm request across a driver-only reinit. */
}

static void session_update(uint32_t full, uint32_t remain, uint8_t discharging) {
    if (!discharging) {
        if (!s_session.active)
            session_reset();
        else if (++s_session.stop_count >= BAT_STOP_SAMPLES)
            BatteryAlgo_Finalize();
        return;
    }

    if (!s_session.active && !s_session.start_count) {
        if (!remain)
            return;
        s_session.start_mwh = s_session.last_mwh = remain;
        s_session.full_mwh = full;
        s_session.start_count = 1u;
        return;
    }

    if (remain > s_session.last_mwh ||
        s_session.start_mwh - remain > s_session.full_mwh + s_session.full_mwh / 4u) {
        session_reset();
        return;
    }
    s_session.last_mwh = remain;
    s_session.stop_count = 0u;
    if (!s_session.active && ++s_session.start_count >= BAT_START_SAMPLES)
        s_session.active = 1u;
}

uint8_t BatteryAlgo_Update(uint32_t full, uint32_t remain, sw6306_learn_state_t learn,
                           uint8_t direction_valid, uint8_t discharging) {
    if (!full_valid(full)) {
        BatteryAlgo_InvalidSample();
        return 0u;
    }
    if (s_full_mwh != full) {
        s_full_mwh = full;
        record_save();
    }
    s_learn = learn;
    uint8_t done = 0u;
    if (learn == SW6306_LEARN_ST_DONE) {
        done = !s_done_seen;
        s_done_seen = s_rearm_pending = 1u;
    } else if (learn != SW6306_LEARN_ST_UNKNOWN) {
        s_done_seen = s_rearm_pending = 0u;
    }
    if (remain > full + full / 4u) {
        session_reset();
    } else if (!direction_valid) {
        s_session.stop_count = 0u;
        if (!s_session.active)
            session_reset();
    } else {
        session_update(full, remain, discharging);
    }
    return done;
}

uint8_t BatteryAlgo_LearnRearmPending(void) {
    return s_rearm_pending && s_learn == SW6306_LEARN_ST_DONE;
}

void BatteryAlgo_LearnRearmed(void) {
    /* Only a fresh non-DONE sample re-arms the notification edge. */
    s_rearm_pending = 0u;
}

void BatteryAlgo_GetView(battery_view_t *out) {
    out->full_mwh = s_full_mwh;
    out->soh_d10 = (uint16_t)((s_full_mwh * 1000u + BAT_FACTORY_ENERGY_MWH / 2u) /
                              BAT_FACTORY_ENERGY_MWH);
    out->efc_q10000 = efc_add(session_delta());
    out->learn = s_learn;
}
