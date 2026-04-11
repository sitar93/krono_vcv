// Included before krono_rhythm_impl.inc inside `namespace krono { namespace { ... } }`.
// Ports firmware mode_gamma_*.c (modes 21–30).

#include <cstring>

static constexpr uint32_t kGcfDefaultTempoIv = 60000u / 70u;

static const Jack kGA[6] = {J1A, J2A, J3A, J4A, J5A, J6A};
static const Jack kGB[6] = {J1B, J2B, J3B, J4B, J5B, J6B};

// --- Mode 21: sequential reset ---
static void g21_init() {
    for (int j = (int)J1A; j <= (int)J6B; j++)
        KR->g_io.set_output(j, false);
}
static void g21_reset() {
    g21_init();
}
static void g21_upd(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    uint32_t c = ctx.f1_counter;
    if (c == 0u)
        return;
    constexpr uint32_t cycle = 12u;
    uint32_t pos = (c - 1u) % cycle;
    if (ctx.calc_mode == CalcMode::Swapped)
        pos = (cycle - 1u) - pos;
    if (pos < 6u)
        KR->g_io.pulse_ms(kGA[pos], DEFAULT_PULSE_MS);
    else
        KR->g_io.pulse_ms(kGB[pos - 6u], DEFAULT_PULSE_MS);
}
static void g21_mod() {
    if (g_eng)
        g_eng->restart_beat_phase_now();
}

// --- Mode 22: sequential freeze ---
static void g22_init() {
    KR->g22_step = 0;
    KR->g22_frozen = false;
    for (int j = (int)J1A; j <= (int)J6B; j++)
        KR->g_io.set_output(j, false);
}
static void g22_reset() {
    g22_init();
}
static void g22_apply_persist(const RhythmPersistState& s) {
    KR->g22_frozen = s.gamma_seq_freeze_frozen;
    KR->g22_step = (s.gamma_seq_freeze_step >= 12u) ? 0u : s.gamma_seq_freeze_step;
}
static void g22_snap(RhythmPersistState& s) {
    s.gamma_seq_freeze_frozen = KR->g22_frozen;
    s.gamma_seq_freeze_step = KR->g22_step;
}
static void g22_upd(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    constexpr uint32_t cycle = 12u;
    uint32_t pos = (uint32_t)(KR->g22_step % cycle);
    if (ctx.calc_mode == CalcMode::Swapped)
        pos = (cycle - 1u) - pos;
    if (pos < 6u)
        KR->g_io.pulse_ms(kGA[pos], DEFAULT_PULSE_MS);
    else
        KR->g_io.pulse_ms(kGB[pos - 6u], DEFAULT_PULSE_MS);
    if (!KR->g22_frozen)
        KR->g22_step = (uint8_t)((KR->g22_step + 1u) % cycle);
}
static void g22_mod() {
    KR->g22_frozen = !KR->g22_frozen;
}

// --- Mode 23: trip patterns ---
#define GAMMA_TRIP_NUM_PATTERNS 6u
#define GAMMA_TRIP_MAX_OUTS 4u
struct GammaTripStep {
    uint8_t n_out;
    Jack outs[GAMMA_TRIP_MAX_OUTS];
};
struct GammaTripDef {
    const GammaTripStep* steps;
    uint8_t num_steps;
};

static const GammaTripStep TRIP_P0[] = {
    {2u, {J1A, J6B}},
    {2u, {J2A, J5B}},
    {2u, {J3A, J4B}},
    {2u, {J4A, J3B}},
    {2u, {J5A, J2B}},
    {2u, {J6A, J1B}},
};
static const GammaTripStep TRIP_P1[] = {
    {2u, {J1A, J6B}}, {2u, {J2A, J5B}}, {2u, {J3A, J4B}}, {2u, {J4A, J3B}},
    {2u, {J5A, J2B}}, {2u, {J6A, J1B}}, {2u, {J5A, J2B}}, {2u, {J4A, J3B}},
    {2u, {J3A, J4B}}, {2u, {J2A, J5B}},
};
static const GammaTripStep TRIP_P2[] = {
    {1u, {J1A}}, {1u, {J1B}}, {1u, {J2A}}, {1u, {J2B}}, {1u, {J3A}}, {1u, {J3B}},
    {1u, {J4A}}, {1u, {J4B}}, {1u, {J5A}}, {1u, {J5B}}, {1u, {J6A}}, {1u, {J6B}},
};
static const GammaTripStep TRIP_P3[] = {
    {1u, {J1A}}, {1u, {J2A}}, {1u, {J3A}}, {1u, {J4A}}, {1u, {J5A}}, {1u, {J6A}},
    {1u, {J6B}}, {1u, {J5B}}, {1u, {J4B}}, {1u, {J3B}}, {1u, {J2B}}, {1u, {J1B}},
};
static const GammaTripStep TRIP_P4[] = {
    {2u, {J1A, J4B}}, {2u, {J6A, J3B}}, {2u, {J2A, J5B}}, {2u, {J5A, J2B}},
    {2u, {J3A, J6B}}, {2u, {J4A, J1B}},
};
static const GammaTripStep TRIP_P5[] = {
    {4u, {J1A, J6A, J3B, J4B}},
    {4u, {J2A, J5A, J2B, J5B}},
    {4u, {J3A, J4A, J1B, J6B}},
};

static const GammaTripDef TRIP_DEFS[GAMMA_TRIP_NUM_PATTERNS] = {
    {TRIP_P0, 6u}, {TRIP_P1, 10u}, {TRIP_P2, 12u}, {TRIP_P3, 12u}, {TRIP_P4, 6u}, {TRIP_P5, 3u},
};

static uint8_t g23_eff_step(uint8_t raw, uint8_t len, bool swapped) {
    if (len == 0u)
        return 0;
    return swapped ? (uint8_t)(len - 1u - raw) : raw;
}

static void g23_init() {
    KR->g23_pat = 0;
    KR->g23_step = 0;
    for (int j = (int)J1A; j <= (int)J6B; j++)
        KR->g_io.set_output(j, false);
}
static void g23_reset() {
    g23_init();
}
static void g23_apply_persist(const RhythmPersistState& s) {
    KR->g23_pat = (s.gamma_seq_trip_pattern >= GAMMA_TRIP_NUM_PATTERNS) ? 0u : s.gamma_seq_trip_pattern;
    uint8_t len = TRIP_DEFS[KR->g23_pat].num_steps;
    KR->g23_step = (len == 0u || s.gamma_seq_trip_step >= len) ? 0u : s.gamma_seq_trip_step;
}
static void g23_snap(RhythmPersistState& s) {
    s.gamma_seq_trip_pattern = KR->g23_pat;
    s.gamma_seq_trip_step = KR->g23_step;
}
static void g23_upd(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    const GammaTripDef& def = TRIP_DEFS[KR->g23_pat];
    uint8_t len = def.num_steps;
    if (len == 0u)
        return;
    bool swapped = (ctx.calc_mode == CalcMode::Swapped);
    uint8_t ei = g23_eff_step(KR->g23_step, len, swapped);
    const GammaTripStep& st = def.steps[ei];
    for (uint8_t k = 0; k < st.n_out && k < GAMMA_TRIP_MAX_OUTS; k++)
        KR->g_io.pulse_ms(st.outs[k], DEFAULT_PULSE_MS);
    KR->g23_step = (uint8_t)((KR->g23_step + 1u) % len);
}
static void g23_mod() {
    KR->g23_pat = (uint8_t)((KR->g23_pat + 1u) % GAMMA_TRIP_NUM_PATTERNS);
    KR->g23_step = 0;
}

// --- Mode 24: fire ---
static void g24_init() {
    KR->g24_fire = -1;
    for (int j = (int)J1A; j <= (int)J6B; j++)
        KR->g_io.set_output(j, false);
}
static void g24_reset() {
    g24_init();
}
static void g24_upd(const ModeContext& ctx) {
    if (KR->g24_fire < 0 || !ctx.f1_rising_edge)
        return;
    if (KR->g24_fire <= 4) {
        uint8_t ai = (uint8_t)KR->g24_fire + 1u;
        uint8_t bi = (uint8_t)(4u - (unsigned)KR->g24_fire);
        KR->g_io.pulse_ms(kGA[ai], DEFAULT_PULSE_MS);
        KR->g_io.pulse_ms(kGB[bi], DEFAULT_PULSE_MS);
        KR->g24_fire++;
        if (KR->g24_fire > 4)
            KR->g24_fire = -1;
    }
}
static void g24_mod() {
    KR->g_io.pulse_ms(kGA[0], DEFAULT_PULSE_MS);
    KR->g_io.pulse_ms(kGB[5], DEFAULT_PULSE_MS);
    KR->g24_fire = 0;
}

// --- Mode 25: bounce ---
#define BOUNCE_CH 12u
#define BOUNCE_PULSES 6u
static const Jack BOUNCE_JACK[12] = {
    J1A, J2A, J3A, J4A, J5A, J6A, J1B, J2B, J3B, J4B, J5B, J6B};

static uint32_t g25_lerp_u32(uint32_t a, uint32_t b, uint8_t step, uint8_t max_step) {
    if (max_step == 0u)
        return a;
    int64_t va = (int64_t)a;
    int64_t vb = (int64_t)b;
    int64_t out = va + (vb - va) * (int64_t)step / (int64_t)max_step;
    return (out < 0) ? 0u : (uint32_t)out;
}

static void g25_accel_gaps(uint8_t row, uint32_t* g5) {
    static const uint32_t g1a[5] = {1000u, 750u, 500u, 333u, 250u};
    static const uint32_t g2a[5] = {750u, 604u, 458u, 313u, 167u};
    static const uint32_t g6a[5] = {180u, 140u, 100u, 70u, 45u};
    for (int k = 0; k < 5; k++) {
        uint32_t t1 = g1a[k];
        uint32_t t2 = g2a[k];
        uint32_t t6 = g6a[k];
        if (row <= 1u)
            g5[k] = (row == 0u) ? t1 : t2;
        else
            g5[k] = g25_lerp_u32(t2, t6, (uint8_t)(row - 1u), 4u);
    }
}

static void g25_decel_gaps(uint8_t row, uint32_t* g5) {
    static const uint32_t g1b[5] = {1000u, 1333u, 1500u, 2000u, 2500u};
    static const uint32_t g6b[5] = {1800u, 2400u, 2700u, 3600u, 4500u};
    for (int k = 0; k < 5; k++)
        g5[k] = g25_lerp_u32(g1b[k], g6b[k], row, 5u);
}

static void g25_build_row(uint8_t idx, uint32_t t0) {
    uint32_t gaps[5];
    uint32_t t = t0;
    KR->g25_abs[idx][0] = t0;
    if (idx < 6u)
        g25_accel_gaps(idx, gaps);
    else
        g25_decel_gaps((uint8_t)(idx - 6u), gaps);
    for (int k = 0; k < 5; k++) {
        t += gaps[k];
        KR->g25_abs[idx][(uint8_t)(k + 1)] = t;
    }
}

static void g25_arm(uint32_t t0) {
    for (uint8_t i = 0u; i < BOUNCE_CH; i++) {
        g25_build_row(i, t0);
        KR->g25_next[i] = 1u;
        KR->g_io.pulse_ms(BOUNCE_JACK[i], DEFAULT_PULSE_MS);
    }
    KR->g25_active = true;
}

static void g25_init() {
    KR->g25_active = false;
    for (uint8_t i = 0u; i < BOUNCE_CH; i++)
        KR->g25_next[i] = BOUNCE_PULSES;
    for (int j = (int)J1A; j <= (int)J6B; j++)
        KR->g_io.set_output(j, false);
}
static void g25_reset() {
    g25_init();
}
static void g25_upd(const ModeContext& ctx) {
    if (!KR->g25_active)
        return;
    uint32_t now = ctx.current_time_ms;
    bool any = false;
    for (uint8_t i = 0u; i < BOUNCE_CH; i++) {
        if (KR->g25_next[i] >= BOUNCE_PULSES)
            continue;
        any = true;
        if (now >= KR->g25_abs[i][KR->g25_next[i]]) {
            KR->g_io.pulse_ms(BOUNCE_JACK[i], DEFAULT_PULSE_MS);
            KR->g25_next[i]++;
        }
    }
    if (!any)
        KR->g25_active = false;
}
static void g25_mod(uint32_t ts) {
    g25_arm(ts);
}

// --- Mode 26: portals ---
static void g26_apply_pair(uint8_t i) {
    bool a_on = KR->g26_a_open[i];
    KR->g_io.set_output(kGA[i], a_on);
    KR->g_io.set_output(kGB[i], !a_on);
}

static void g26_toggle_pair(uint8_t i) {
    KR->g26_a_open[i] = !KR->g26_a_open[i];
    g26_apply_pair(i);
}

static void g26_init() {
    uint32_t now = g_eng ? g_eng->current_time_ms() : 0u;
    std::memset(KR->g26_f1cnt, 0, sizeof(KR->g26_f1cnt));
    for (uint8_t i = 0u; i < 6u; i++) {
        KR->g26_a_open[i] = true;
        KR->g26_last_swap[i] = now;
        g26_apply_pair(i);
    }
}
static void g26_reset() {
    g26_init();
}
static void g26_apply_persist(const RhythmPersistState& s) {
    KR->g26_mult = s.gamma_portals_div_on_a;
}
static void g26_snap(RhythmPersistState& s) {
    s.gamma_portals_div_on_a = KR->g26_mult;
}
static void g26_upd(const ModeContext& ctx) {
    uint32_t now = ctx.current_time_ms;
    uint32_t T = ctx.current_tempo_interval_ms;
    if (T < MIN_INTERVAL || T > MAX_INTERVAL)
        return;

    if (!KR->g26_mult) {
        if (!ctx.f1_rising_edge)
            return;
        for (uint8_t i = 0u; i < 6u; i++) {
            uint32_t k = (uint32_t)i + 1u;
            KR->g26_f1cnt[i]++;
            if ((uint32_t)KR->g26_f1cnt[i] >= k) {
                KR->g26_f1cnt[i] = 0;
                g26_toggle_pair(i);
            }
        }
        return;
    }

    for (uint8_t i = 0u; i < 6u; i++) {
        uint32_t k = (uint32_t)i + 1u;
        uint32_t per = T / k;
        if (per < MIN_CLOCK_INTERVAL)
            per = MIN_CLOCK_INTERVAL;
        uint8_t guard = 0u;
        while (now >= KR->g26_last_swap[i] + per && guard < 8u) {
            g26_toggle_pair(i);
            KR->g26_last_swap[i] += per;
            guard++;
        }
        if (guard >= 8u)
            KR->g26_last_swap[i] = now;
    }
}
static void g26_mod(uint32_t now) {
    KR->g26_mult = !KR->g26_mult;
    std::memset(KR->g26_f1cnt, 0, sizeof(KR->g26_f1cnt));
    for (uint8_t i = 0u; i < 6u; i++)
        KR->g26_last_swap[i] = now;
}

// --- Mode 27: coin toss ---
static const uint8_t COIN_P_A[6] = {85u, 75u, 65u, 50u, 25u, 15u};
static void g27_init() {
    for (int j = (int)J1A; j <= (int)J6B; j++)
        KR->g_io.set_output(j, false);
}
static void g27_reset() {
    g27_init();
}
static void g27_apply_persist(const RhythmPersistState& s) {
    KR->g27_inv = s.gamma_coin_invert;
}
static void g27_snap(RhythmPersistState& s) {
    s.gamma_coin_invert = KR->g27_inv;
}
static void g27_upd(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    for (int i = 0; i < 6; i++) {
        uint8_t p_a = COIN_P_A[i];
        if (KR->g27_inv)
            p_a = (uint8_t)(100u - (unsigned)p_a);
        uint8_t r = (uint8_t)(std::rand() % 100);
        if (r < p_a)
            KR->g_io.pulse_ms(kGA[i], DEFAULT_PULSE_MS);
        else
            KR->g_io.pulse_ms(kGB[i], DEFAULT_PULSE_MS);
    }
}
static void g27_mod() {
    KR->g27_inv = !KR->g27_inv;
}

// --- Modes 28–30: ratchet family (shared) ---
static const uint8_t GCF_F[6] = {1u, 2u, 3u, 4u, 5u, 6u};

static uint32_t g_gcf_ratchet_Teff(uint32_t T, bool dbl) {
    if (T < MIN_INTERVAL)
        T = MIN_INTERVAL;
    if (T > MAX_INTERVAL)
        T = MAX_INTERVAL;
    if (KR->g_gcf_var != GcfVar::Ratchet || !dbl)
        return T;
    uint32_t h = T / 2u;
    return (h < MIN_INTERVAL) ? MIN_INTERVAL : h;
}

static uint32_t g_gcf_anti_Teff(uint32_t T, bool half) {
    if (T < MIN_INTERVAL)
        T = MIN_INTERVAL;
    if (T > MAX_INTERVAL)
        T = MAX_INTERVAL;
    if (KR->g_gcf_var != GcfVar::AntiRatchet || !half)
        return T;
    uint32_t d = T * 2u;
    return (d > MAX_INTERVAL) ? MAX_INTERVAL : d;
}

static uint32_t g_gcf_effective_T(uint32_t T) {
    if (KR->g_gcf_var == GcfVar::Ratchet)
        return g_gcf_ratchet_Teff(T, KR->g_gcf_ratchet_double);
    if (KR->g_gcf_var == GcfVar::AntiRatchet)
        return g_gcf_anti_Teff(T, KR->g_gcf_anti_half);
    return T;
}

static bool g_gcf_outputs_muted() {
    return (KR->g_gcf_var == GcfVar::StartStop) && KR->g_gcf_muted;
}

static void g_gcf_all_low() {
    for (int i = 0; i < 6; i++) {
        KR->g_io.set_output(kGA[i], false);
        KR->g_io.set_output(kGB[i], false);
    }
}

static void g_gcf_resync_phase(uint32_t now) {
    std::memset(KR->g_gcf_div_ctr, 0, sizeof(KR->g_gcf_div_ctr));
    for (int j = 0; j < 12; j++)
        KR->g_gcf_next_mult[j] = now;
    g_gcf_all_low();
    KR->g_gcf_wait_f1 = true;
}

static void g_gcf_scale_mult(uint32_t now, uint32_t old_teff, uint32_t new_teff) {
    if (old_teff == 0u || new_teff == 0u || old_teff == new_teff)
        return;
    for (int i = 0; i < 6; i++) {
        Jack pa = kGA[i];
        int pj = (int)pa;
        uint32_t n = KR->g_gcf_next_mult[pj];
        if (n > now) {
            uint64_t rem = (uint64_t)(n - now);
            rem = rem * (uint64_t)new_teff / (uint64_t)old_teff;
            if (rem > (uint64_t)MAX_INTERVAL * 4u)
                rem = MAX_INTERVAL;
            KR->g_gcf_next_mult[pj] = now + (uint32_t)rem;
        }
    }
}

static void g28_common_init(GcfVar v) {
    KR->g_gcf_var = v;
    uint32_t now = g_eng ? g_eng->current_time_ms() : 0u;
    g_gcf_resync_phase(now);
}

static void g28_init() {
    g28_common_init(GcfVar::Ratchet);
}
static void g29_init() {
    g28_common_init(GcfVar::AntiRatchet);
}
static void g30_init() {
    g28_common_init(GcfVar::StartStop);
}

static void g28_reset() {
    g28_common_init(GcfVar::Ratchet);
}
static void g29_reset() {
    g28_common_init(GcfVar::AntiRatchet);
}
static void g30_reset() {
    g28_common_init(GcfVar::StartStop);
}

static void g28_apply_persist(const RhythmPersistState& s) {
    KR->g_gcf_ratchet_double = s.gamma_ratchet_double;
}
static void g29_apply_persist(const RhythmPersistState& s) {
    KR->g_gcf_anti_half = s.gamma_antiratchet_half;
}
static void g30_apply_persist(const RhythmPersistState& s) {
    KR->g_gcf_muted = s.gamma_startstop_muted;
}
static void g28_snap(RhythmPersistState& s) {
    s.gamma_ratchet_double = KR->g_gcf_ratchet_double;
}
static void g29_snap(RhythmPersistState& s) {
    s.gamma_antiratchet_half = KR->g_gcf_anti_half;
}
static void g30_snap(RhythmPersistState& s) {
    s.gamma_startstop_muted = KR->g_gcf_muted;
}

static void gcf_shared_upd(const ModeContext& ctx) {
    uint32_t current_time = ctx.current_time_ms;
    uint32_t raw_T = ctx.current_tempo_interval_ms;
    if (raw_T >= MIN_INTERVAL && raw_T <= MAX_INTERVAL)
        KR->g_gcf_base_T = raw_T;
    uint32_t tempo_interval = g_gcf_effective_T(KR->g_gcf_base_T);
    bool tempo_valid = (tempo_interval >= MIN_INTERVAL && tempo_interval <= MAX_INTERVAL);
    const bool muted = g_gcf_outputs_muted();

    if (KR->g_gcf_wait_f1) {
        if (ctx.f1_rising_edge && tempo_valid) {
            KR->g_gcf_wait_f1 = false;
            uint32_t first_sync_time = current_time;
            std::memset(KR->g_gcf_div_ctr, 0, sizeof(KR->g_gcf_div_ctr));
            for (int i = 0; i < 6; i++) {
                uint32_t factor = GCF_F[i];
                if (factor == 0u)
                    continue;
                uint32_t mult_interval = tempo_interval / factor;
                if (mult_interval < MIN_CLOCK_INTERVAL)
                    mult_interval = MIN_CLOCK_INTERVAL;
                Jack pin_a = kGA[i];
                Jack pin_b = kGB[i];
                int ia = (int)pin_a;
                int ib = (int)pin_b;
                KR->g_gcf_next_mult[ia] = first_sync_time + mult_interval;
                KR->g_gcf_next_mult[ib] = first_sync_time + mult_interval;
                if (!muted && current_time >= KR->g_gcf_next_mult[ia]) {
                    KR->g_io.pulse_ms(pin_a, DEFAULT_PULSE_MS);
                    KR->g_gcf_next_mult[ia] += mult_interval;
                }
            }
        }
        return;
    }

    if (!tempo_valid)
        return;

    for (int i = 0; i < 6; i++) {
        uint32_t factor = GCF_F[i];
        if (factor == 0u)
            continue;
        Jack pin_a = kGA[i];
        Jack pin_b = kGB[i];
        int ia = (int)pin_a;
        int ib = (int)pin_b;
        uint32_t mult_interval = tempo_interval / factor;
        if (mult_interval < MIN_CLOCK_INTERVAL)
            mult_interval = MIN_CLOCK_INTERVAL;

        if (current_time >= KR->g_gcf_next_mult[ia]) {
            if (!muted)
                KR->g_io.pulse_ms(pin_a, DEFAULT_PULSE_MS);
            KR->g_gcf_next_mult[ia] += mult_interval;
            if (KR->g_gcf_next_mult[ia] < current_time)
                KR->g_gcf_next_mult[ia] = current_time + mult_interval;
        }

        if (ctx.f1_rising_edge) {
            KR->g_gcf_div_ctr[ib]++;
            if (KR->g_gcf_div_ctr[ib] >= factor) {
                if (!muted)
                    KR->g_io.pulse_ms(pin_b, DEFAULT_PULSE_MS);
                KR->g_gcf_div_ctr[ib] = 0;
            }
        }
    }
}

static void g28_upd(const ModeContext& ctx) {
    KR->g_gcf_var = GcfVar::Ratchet;
    gcf_shared_upd(ctx);
}
static void g29_upd(const ModeContext& ctx) {
    KR->g_gcf_var = GcfVar::AntiRatchet;
    gcf_shared_upd(ctx);
}
static void g30_upd(const ModeContext& ctx) {
    KR->g_gcf_var = GcfVar::StartStop;
    gcf_shared_upd(ctx);
}

static void g28_mod(uint32_t now) {
    KR->g_gcf_var = GcfVar::Ratchet;
    uint32_t base = KR->g_gcf_base_T;
    if (base < MIN_INTERVAL || base > MAX_INTERVAL)
        base = kGcfDefaultTempoIv;
    bool was = KR->g_gcf_ratchet_double;
    uint32_t old_e = g_gcf_ratchet_Teff(base, was);
    KR->g_gcf_ratchet_double = !was;
    uint32_t new_e = g_gcf_ratchet_Teff(base, KR->g_gcf_ratchet_double);
    g_gcf_scale_mult(now, old_e, new_e);
}

static void g29_mod(uint32_t now) {
    KR->g_gcf_var = GcfVar::AntiRatchet;
    uint32_t base = KR->g_gcf_base_T;
    if (base < MIN_INTERVAL || base > MAX_INTERVAL)
        base = kGcfDefaultTempoIv;
    bool was = KR->g_gcf_anti_half;
    uint32_t old_e = g_gcf_anti_Teff(base, was);
    KR->g_gcf_anti_half = !was;
    uint32_t new_e = g_gcf_anti_Teff(base, KR->g_gcf_anti_half);
    g_gcf_scale_mult(now, old_e, new_e);
}

static void g30_mod() {
    KR->g_gcf_var = GcfVar::StartStop;
    KR->g_gcf_muted = !KR->g_gcf_muted;
}

static void gamma_snapshot_impl(RhythmPersistState& s) {
    g22_snap(s);
    g23_snap(s);
    g26_snap(s);
    g27_snap(s);
    g28_snap(s);
    g29_snap(s);
    g30_snap(s);
}

static void gamma_apply_persist_impl(const RhythmPersistState& s) {
    g22_apply_persist(s);
    g23_apply_persist(s);
    g26_apply_persist(s);
    g27_apply_persist(s);
    g28_apply_persist(s);
    g29_apply_persist(s);
    g30_apply_persist(s);
}

static void gamma_reset_mode(OpMode m) {
    switch (m) {
        case OpMode::GammaSequentialReset:
            g21_reset();
            break;
        case OpMode::GammaSequentialFreeze:
            g22_reset();
            break;
        case OpMode::GammaSequentialTrip:
            g23_reset();
            break;
        case OpMode::GammaSequentialFire:
            g24_reset();
            break;
        case OpMode::GammaSequentialBounce:
            g25_reset();
            break;
        case OpMode::GammaPortals:
            g26_reset();
            break;
        case OpMode::GammaCoinToss:
            g27_reset();
            break;
        case OpMode::GammaRatchet:
            g28_reset();
            break;
        case OpMode::GammaAntiRatchet:
            g29_reset();
            break;
        case OpMode::GammaStartStop:
            g30_reset();
            break;
        default:
            break;
    }
}

static void gamma_init_mode(OpMode m) {
    switch (m) {
        case OpMode::GammaSequentialReset:
            g21_init();
            break;
        case OpMode::GammaSequentialFreeze:
            g22_init();
            break;
        case OpMode::GammaSequentialTrip:
            g23_init();
            break;
        case OpMode::GammaSequentialFire:
            g24_init();
            break;
        case OpMode::GammaSequentialBounce:
            g25_init();
            break;
        case OpMode::GammaPortals:
            g26_init();
            break;
        case OpMode::GammaCoinToss:
            g27_init();
            break;
        case OpMode::GammaRatchet:
            g28_init();
            break;
        case OpMode::GammaAntiRatchet:
            g29_init();
            break;
        case OpMode::GammaStartStop:
            g30_init();
            break;
        default:
            break;
    }
}

static void gamma_update_mode(OpMode m, const ModeContext& ctx) {
    switch (m) {
        case OpMode::GammaSequentialReset:
            g21_upd(ctx);
            break;
        case OpMode::GammaSequentialFreeze:
            g22_upd(ctx);
            break;
        case OpMode::GammaSequentialTrip:
            g23_upd(ctx);
            break;
        case OpMode::GammaSequentialFire:
            g24_upd(ctx);
            break;
        case OpMode::GammaSequentialBounce:
            g25_upd(ctx);
            break;
        case OpMode::GammaPortals:
            g26_upd(ctx);
            break;
        case OpMode::GammaCoinToss:
            g27_upd(ctx);
            break;
        case OpMode::GammaRatchet:
            g28_upd(ctx);
            break;
        case OpMode::GammaAntiRatchet:
            g29_upd(ctx);
            break;
        case OpMode::GammaStartStop:
            g30_upd(ctx);
            break;
        default:
            break;
    }
}

static void gamma_mod_mode(OpMode m, uint32_t ts) {
    switch (m) {
        case OpMode::GammaSequentialReset:
            g21_mod();
            break;
        case OpMode::GammaSequentialFreeze:
            g22_mod();
            break;
        case OpMode::GammaSequentialTrip:
            g23_mod();
            break;
        case OpMode::GammaSequentialFire:
            g24_mod();
            break;
        case OpMode::GammaSequentialBounce:
            g25_mod(ts);
            break;
        case OpMode::GammaPortals:
            g26_mod(ts);
            break;
        case OpMode::GammaCoinToss:
            g27_mod();
            break;
        case OpMode::GammaRatchet:
            g28_mod(ts);
            break;
        case OpMode::GammaAntiRatchet:
            g29_mod(ts);
            break;
        case OpMode::GammaStartStop:
            g30_mod();
            break;
        default:
            break;
    }
}
