#include "krono_hw_engine.hpp"
#include "krono_binary_patterns.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace krono {

void (*g_hw_rhythm_snapshot)(RhythmPersistState*) = nullptr;
void (*g_hw_rhythm_apply)(const RhythmPersistState*) = nullptr;
void (*g_hw_rhythm_overlay)(const RhythmPersistState*, OpMode) = nullptr;
void (*g_hw_rhythm_mod)(OpMode, uint32_t) = nullptr;

namespace {

static HwEngine* g_eng = nullptr;

constexpr int K_NUM_FW_JACKS = 18;
constexpr int K_NUM_OUTS = 12;

constexpr uint32_t MIN_INTERVAL = 33;
constexpr uint32_t MAX_INTERVAL = 6000;
constexpr uint32_t MIN_CLOCK_INTERVAL = 10;
constexpr uint32_t DEFAULT_PULSE_MS = 10;
constexpr uint32_t NUM_INTERVALS_FOR_AVG = 3;
constexpr uint32_t MAX_INTERVAL_DIFFERENCE = 3000;
constexpr uint32_t EXT_CLOCK_TIMEOUT_MS = 5000;
constexpr int NUM_EXT_INTERVALS_FOR_VALIDATION = 3;
constexpr uint32_t CHAOS_DIVISOR_DEFAULT = 1000;
constexpr uint32_t CHAOS_DIVISOR_STEP = 50;
constexpr uint32_t CHAOS_DIVISOR_MIN = 10;
constexpr int NUM_SWING_PROFILES = 8;
constexpr int NUM_BINARY_BANKS = 10;

static void chaos_validate_divisor(uint32_t& div) {
    if (div < CHAOS_DIVISOR_MIN || div > CHAOS_DIVISOR_DEFAULT || (div % CHAOS_DIVISOR_STEP != 0))
        div = CHAOS_DIVISOR_DEFAULT;
}

// Jack indices 0..11 = 1A..6B; 12..17 unused in Rack but used in some arrays.
enum Jack : int {
    J1A = 0,
    J2A,
    J3A,
    J4A,
    J5A,
    J6A,
    J1B,
    J2B,
    J3B,
    J4B,
    J5B,
    J6B
};

struct IoHub {
    float lvl[K_NUM_FW_JACKS];
    float pulse_rem[K_NUM_FW_JACKS];

    IoHub() { clear(); }

    void clear() {
        std::memset(lvl, 0, sizeof(lvl));
        std::memset(pulse_rem, 0, sizeof(pulse_rem));
    }

    void set_output(int j, bool high) {
        if (j >= 0 && j < K_NUM_FW_JACKS)
            lvl[j] = high ? 10.f : 0.f;
    }

    void pulse_ms(int j, uint32_t ms) {
        if (j < 0 || j >= K_NUM_FW_JACKS)
            return;
        float s = ms * 1e-3f;
        pulse_rem[j] = std::max(pulse_rem[j], s);
    }

    void step_audio(float dt) {
        for (int j = 0; j < K_NUM_FW_JACKS; j++) {
            if (pulse_rem[j] > 0.f) {
                pulse_rem[j] -= dt;
                if (pulse_rem[j] <= 0.f)
                    pulse_rem[j] = 0.f;
            }
        }
    }

    void to_rack(float out[K_NUM_OUTS]) const {
        for (int i = 0; i < K_NUM_OUTS; i++) {
            if (pulse_rem[i] > 0.f)
                out[i] = 10.f;
            else
                out[i] = lvl[i];
        }
    }
};

static IoHub g_io;

// --- Tap tempo (mirrors firmware input_handler tap averaging) ---
static uint32_t g_tap_ivals[NUM_INTERVALS_FOR_AVG] = {};
static uint8_t g_tap_idx = 0;

// --- External clock validation (ext_clock.c simplified, polled) ---
static uint32_t g_ext_last_rise_ms = 0;
static uint32_t g_ext_last_isr_ms = 0;
static uint32_t g_ext_validated_iv = 0;
static uint32_t g_ext_validated_ev_ms = 0;
static uint32_t g_ext_ivals[NUM_EXT_INTERVALS_FOR_VALIDATION] = {};
static uint8_t g_ext_idx = 0;
static bool g_ext_active = false;

static void reset_tap_calc() {
    g_tap_idx = 0;
    for (auto& v : g_tap_ivals)
        v = 0;
}

static void reset_ext_validation() {
    g_ext_idx = 0;
    for (auto& v : g_ext_ivals)
        v = 0;
}

static bool ext_timed_out(uint32_t now_ms) {
    if (g_ext_last_isr_ms == 0)
        return true;
    return now_ms > g_ext_last_isr_ms + EXT_CLOCK_TIMEOUT_MS;
}

// --- mode_default ---
static uint32_t g_def_next_mult[K_NUM_FW_JACKS] = {};
static uint32_t g_def_div_ctr[K_NUM_FW_JACKS] = {};
static bool g_def_wait_f1 = false;
static const uint32_t g_def_factors[5] = {2, 3, 4, 5, 6};
static const int g_def_pin_a[5] = {J2A, J3A, J4A, J5A, J6A};
static const int g_def_pin_b[5] = {J2B, J3B, J4B, J5B, J6B};

static void mode_default_reset() {
    std::memset(g_def_div_ctr, 0, sizeof(g_def_div_ctr));
    for (int j = 0; j < K_NUM_FW_JACKS; j++)
        g_def_next_mult[j] = 0;
    for (int i = 0; i < 5; i++) {
        g_io.set_output(g_def_pin_a[i], false);
        g_io.set_output(g_def_pin_b[i], false);
    }
    g_def_wait_f1 = true;
}

static void mode_default_init() {
    mode_default_reset();
}

static void mode_default_update(const ModeContext& ctx) {
    uint32_t t = ctx.current_time_ms;
    uint32_t ti = ctx.current_tempo_interval_ms;
    bool tempo_ok = (ti >= MIN_INTERVAL && ti <= MAX_INTERVAL);
    bool mult_a = (ctx.calc_mode == CalcMode::Normal);

    if (g_def_wait_f1) {
        if (ctx.f1_rising_edge && tempo_ok) {
            g_def_wait_f1 = false;
            uint32_t sync_t = t;
            std::memset(g_def_div_ctr, 0, sizeof(g_def_div_ctr));
            for (int i = 0; i < 5; i++) {
                uint32_t f = g_def_factors[i];
                uint32_t mult_iv = ti / f;
                if (mult_iv < MIN_CLOCK_INTERVAL)
                    mult_iv = MIN_CLOCK_INTERVAL;
                int pa = g_def_pin_a[i];
                int pb = g_def_pin_b[i];
                int mp = mult_a ? pa : pb;
                int op = mult_a ? pb : pa;
                (void)op;
                g_def_next_mult[mp] = sync_t + mult_iv;
                g_def_next_mult[mult_a ? pb : pa] = sync_t + mult_iv;
                if (t >= g_def_next_mult[mp]) {
                    g_io.pulse_ms(mp, DEFAULT_PULSE_MS);
                    g_def_next_mult[mp] += mult_iv;
                }
            }
            return;
        }
        return;
    }

    if (!tempo_ok)
        return;

    for (int i = 0; i < 5; i++) {
        uint32_t f = g_def_factors[i];
        int pa = g_def_pin_a[i];
        int pb = g_def_pin_b[i];
        int mp = mult_a ? pa : pb;
        int dp = mult_a ? pb : pa;
        uint32_t mult_iv = ti / f;
        if (mult_iv < MIN_CLOCK_INTERVAL)
            mult_iv = MIN_CLOCK_INTERVAL;
        if (t >= g_def_next_mult[mp]) {
            g_io.pulse_ms(mp, DEFAULT_PULSE_MS);
            g_def_next_mult[mp] += mult_iv;
            if (g_def_next_mult[mp] < t)
                g_def_next_mult[mp] = t + mult_iv;
        }
        if (ctx.f1_rising_edge) {
            g_def_div_ctr[dp]++;
            if (g_def_div_ctr[dp] >= (int)f) {
                g_io.pulse_ms(dp, DEFAULT_PULSE_MS);
                g_def_div_ctr[dp] = 0;
            }
        }
    }
}

// --- Euclidean ---
static uint32_t g_euc_step_a[5] = {};
static uint32_t g_euc_step_b[5] = {};
static const uint8_t g_euc_k1[5] = {2, 3, 3, 4, 5};
static const uint8_t g_euc_n1[5] = {5, 7, 8, 9, 11};
static const uint8_t g_euc_k2[5] = {3, 4, 5, 6, 7};
static const uint8_t g_euc_n2[5] = {4, 6, 7, 8, 9};
static const int g_euc_pa[5] = {J2A, J3A, J4A, J5A, J6A};
static const int g_euc_pb[5] = {J2B, J3B, J4B, J5B, J6B};

static bool euc_pulse(uint8_t k, uint8_t n, uint32_t step) {
    if (n == 0 || k == 0)
        return false;
    if (k >= n)
        return true;
    uint32_t cm = step % n;
    uint32_t pm = (cm == 0U) ? (uint32_t)(n - 1) : cm - 1;
    uint32_t cf = (cm * k) / n;
    uint32_t pf = (pm * k) / n;
    return cf != pf;
}

static void mode_euclidean_reset() {
    for (int i = 0; i < 5; i++) {
        g_io.set_output(g_euc_pa[i], false);
        g_io.set_output(g_euc_pb[i], false);
        g_euc_step_a[i] = g_euc_step_b[i] = 0;
    }
}

static void mode_euclidean_init() {
    mode_euclidean_reset();
}

static void mode_euclidean_update(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    bool s1a = (ctx.calc_mode == CalcMode::Normal);
    for (int i = 0; i < 5; i++) {
        const uint8_t* ka = s1a ? g_euc_k1 : g_euc_k2;
        const uint8_t* na = s1a ? g_euc_n1 : g_euc_n2;
        const uint8_t* kb = s1a ? g_euc_k2 : g_euc_k1;
        const uint8_t* nb = s1a ? g_euc_n2 : g_euc_n1;
        g_euc_step_a[i]++;
        bool pa = euc_pulse(ka[i], na[i], g_euc_step_a[i]);
        if (na[i] > 0)
            g_euc_step_a[i] %= na[i];
        else
            g_euc_step_a[i] = 0;
        if (pa)
            g_io.pulse_ms(g_euc_pa[i], DEFAULT_PULSE_MS);

        g_euc_step_b[i]++;
        bool pb = euc_pulse(kb[i], nb[i], g_euc_step_b[i]);
        if (nb[i] > 0)
            g_euc_step_b[i] %= nb[i];
        else
            g_euc_step_b[i] = 0;
        if (pb)
            g_io.pulse_ms(g_euc_pb[i], DEFAULT_PULSE_MS);
    }
}

// --- Probabilistic ---
static const float g_pr_a[5] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
static const float g_pr_b[5] = {0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};
static const int g_pr_ja[5] = {J2A, J3A, J4A, J5A, J6A};
static const int g_pr_jb[5] = {J2B, J3B, J4B, J5B, J6B};

static void mode_prob_reset() {
    for (int i = 0; i < 5; i++) {
        g_io.set_output(g_pr_ja[i], false);
        g_io.set_output(g_pr_jb[i], false);
    }
}

static void mode_prob_init() {
    std::srand((unsigned)std::time(nullptr));
    mode_prob_reset();
}

static void mode_prob_update(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    const float* pa = (ctx.calc_mode == CalcMode::Normal) ? g_pr_a : g_pr_b;
    const float* pb = (ctx.calc_mode == CalcMode::Normal) ? g_pr_b : g_pr_a;
    for (int i = 0; i < 5; i++) {
        float r = (float)std::rand() / (float)RAND_MAX;
        if (r < pa[i])
            g_io.pulse_ms(g_pr_ja[i], DEFAULT_PULSE_MS);
    }
    for (int i = 0; i < 5; i++) {
        float r = (float)std::rand() / (float)RAND_MAX;
        if (r < pb[i])
            g_io.pulse_ms(g_pr_jb[i], DEFAULT_PULSE_MS);
    }
}

// --- Sequential ---
static const uint32_t g_sq_fib[4] = {1, 2, 3, 5};
static const uint32_t g_sq_pri[4] = {2, 3, 5, 7};
static const uint32_t g_sq_luc[4] = {2, 1, 3, 4};
static const uint32_t g_sq_comp[4] = {4, 6, 8, 9};
static const int g_sq_ja[5] = {J2A, J3A, J4A, J5A, J6A};
static const int g_sq_jb[5] = {J2B, J3B, J4B, J5B, J6B};

static void mode_seq_reset() {
    for (int i = 0; i < 5; i++) {
        g_io.set_output(g_sq_ja[i], false);
        g_io.set_output(g_sq_jb[i], false);
    }
}

static void mode_seq_init() {
    mode_seq_reset();
}

static void mode_seq_update(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    uint32_t c = ctx.f1_counter;
    const uint32_t* sa = (ctx.calc_mode == CalcMode::Normal) ? g_sq_fib : g_sq_luc;
    const uint32_t* sb = (ctx.calc_mode == CalcMode::Normal) ? g_sq_pri : g_sq_comp;
    bool suma = false, sumb = false;
    for (int i = 0; i < 4; i++) {
        uint32_t da = sa[i];
        if (da > 0 && (c % da) == 0) {
            g_io.pulse_ms(g_sq_ja[i], DEFAULT_PULSE_MS);
            suma = true;
        }
        uint32_t db = sb[i];
        if (db > 0 && (c % db) == 0) {
            g_io.pulse_ms(g_sq_jb[i], DEFAULT_PULSE_MS);
            sumb = true;
        }
    }
    if (suma)
        g_io.pulse_ms(J6A, DEFAULT_PULSE_MS);
    if (sumb)
        g_io.pulse_ms(J6B, DEFAULT_PULSE_MS);
}

// --- Musical ---
static const uint16_t g_mu_n1[5] = {1, 1, 8, 6, 4};
static const uint16_t g_mu_d1[5] = {6, 8, 1, 5, 5};
static const uint16_t g_mu_n2[5] = {1, 3, 5, 7, 9};
static const uint16_t g_mu_d2[5] = {7, 4, 3, 2, 4};
static const int g_mu_pa[5] = {J2A, J3A, J4A, J5A, J6A};
static const int g_mu_pb[5] = {J2B, J3B, J4B, J5B, J6B};
static uint32_t g_mu_last_a[5] = {};
static uint32_t g_mu_last_b[5] = {};
static bool g_mu_st_a[5] = {};
static bool g_mu_st_b[5] = {};

static void mode_musical_reset() {
    for (int i = 0; i < 5; i++) {
        g_io.set_output(g_mu_pa[i], false);
        g_io.set_output(g_mu_pb[i], false);
        g_mu_last_a[i] = g_mu_last_b[i] = 0;
        g_mu_st_a[i] = g_mu_st_b[i] = false;
    }
}

static void mode_musical_init() {
    mode_musical_reset();
}

static void mode_musical_update(const ModeContext& ctx) {
    bool s1a = (ctx.calc_mode == CalcMode::Normal);
    bool tv = (ctx.current_tempo_interval_ms >= MIN_INTERVAL && ctx.current_tempo_interval_ms <= MAX_INTERVAL);
    uint32_t t = ctx.current_time_ms;
    for (int i = 0; i < 5; i++) {
        uint32_t iv1 = UINT32_MAX, iv2 = UINT32_MAX;
        if (tv) {
            if (g_mu_d1[i] > 0)
                iv1 = (uint32_t)ctx.current_tempo_interval_ms * g_mu_n1[i] / g_mu_d1[i];
            if (g_mu_d2[i] > 0)
                iv2 = (uint32_t)ctx.current_tempo_interval_ms * g_mu_n2[i] / g_mu_d2[i];
        }
        if (iv1 < MIN_CLOCK_INTERVAL)
            iv1 = MIN_CLOCK_INTERVAL;
        if (iv2 < MIN_CLOCK_INTERVAL)
            iv2 = MIN_CLOCK_INTERVAL;
        uint32_t iva = s1a ? iv1 : iv2;
        uint32_t ivb = s1a ? iv2 : iv1;

        auto proc = [&](int pin, uint32_t iv, uint32_t* lt, bool* st) {
            if (iv == UINT32_MAX) {
                if (*st) {
                    *st = false;
                    g_io.set_output(pin, false);
                }
                return;
            }
            uint32_t pw = DEFAULT_PULSE_MS;
            if (pw >= iv)
                pw = iv > 1 ? 1 : 0;
            if (pw == 0 && iv > 0)
                pw = 1;
            uint32_t dur = *st ? pw : (iv > pw ? iv - pw : 1);
            if (dur == 0)
                dur = 1;
            if (t - *lt >= dur) {
                *st = !(*st);
                g_io.set_output(pin, *st);
                *lt = t;
            }
        };
        proc(g_mu_pa[i], iva, &g_mu_last_a[i], &g_mu_st_a[i]);
        proc(g_mu_pb[i], ivb, &g_mu_last_b[i], &g_mu_st_b[i]);
    }
}

// --- Swing ---
static const uint8_t g_sw_p0[5] = {50, 50, 50, 50, 50};
static const uint8_t g_sw_p1[5] = {52, 53, 54, 55, 56};
static const uint8_t g_sw_p2[5] = {55, 57, 59, 61, 63};
static const uint8_t g_sw_p3[5] = {58, 61, 64, 67, 70};
static const uint8_t g_sw_p4[5] = {62, 65, 68, 71, 74};
static const uint8_t g_sw_p5[5] = {66, 69, 72, 75, 78};
static const uint8_t g_sw_p6[5] = {70, 73, 76, 79, 82};
static const uint8_t g_sw_p7[5] = {75, 78, 81, 84, 87};
static const uint8_t* const g_sw_prof[8] = {g_sw_p0, g_sw_p1, g_sw_p2, g_sw_p3, g_sw_p4, g_sw_p5, g_sw_p6, g_sw_p7};

static uint32_t g_sw_on[K_NUM_FW_JACKS] = {};
static uint32_t g_sw_off[K_NUM_FW_JACKS] = {};

static uint32_t swing_delay(uint32_t beat_ix, uint32_t tempo, uint8_t pct) {
    if (beat_ix % 2 == 0 || pct <= 50)
        return 0;
    int32_t d = ((int32_t)pct - 50) * (int32_t)tempo / 100;
    return d > 0 ? (uint32_t)d : 0;
}

static void mode_swing_reset() {
    std::memset(g_sw_on, 0, sizeof(g_sw_on));
    std::memset(g_sw_off, 0, sizeof(g_sw_off));
    for (int p = J1A; p <= J6B; p++) {
        g_io.set_output(p, false);
    }
}

static void mode_swing_init() {
    mode_swing_reset();
}

static void mode_swing_update(const ModeContext& ctx, uint32_t& idx_a, uint32_t& idx_b) {
    uint32_t t = ctx.current_time_ms;
    if (ctx.calc_mode_changed) {
        idx_a = (idx_a + 1) % NUM_SWING_PROFILES;
        idx_b = (idx_b + NUM_SWING_PROFILES - 1) % NUM_SWING_PROFILES;
    }
    for (int pin = J1A; pin <= J6B; pin++) {
        if (g_sw_on[pin] != 0 && t >= g_sw_on[pin]) {
            if (g_sw_off[pin] == 0) {
                g_io.set_output(pin, true);
                g_sw_off[pin] = t + DEFAULT_PULSE_MS;
            }
            g_sw_on[pin] = 0;
        }
    }
    for (int pin = J1A; pin <= J6B; pin++) {
        if (g_sw_off[pin] != 0 && t >= g_sw_off[pin]) {
            g_io.set_output(pin, false);
            g_sw_off[pin] = 0;
        }
    }
    if (ctx.f1_rising_edge) {
        uint32_t beat = (ctx.f1_counter - 1) % 4;
        const uint8_t* pa = g_sw_prof[idx_a % NUM_SWING_PROFILES];
        const uint8_t* pb = g_sw_prof[idx_b % NUM_SWING_PROFILES];
        for (int pin = J1A; pin <= J6A; pin++) {
            uint32_t dly = 0;
            if (pin >= J2A) {
                int ix = pin - J2A;
                if (ix < 5)
                    dly = swing_delay(beat, ctx.current_tempo_interval_ms, pa[ix]);
            }
            uint32_t ton = t + dly;
            if (g_sw_off[pin] == 0 && g_sw_on[pin] == 0) {
                if (dly == 0) {
                    g_io.set_output(pin, true);
                    g_sw_off[pin] = ton + DEFAULT_PULSE_MS;
                } else {
                    g_sw_on[pin] = ton;
                }
            }
        }
        for (int pin = J1B; pin <= J6B; pin++) {
            uint32_t dly = 0;
            if (pin >= J2B) {
                int ix = pin - J2B;
                if (ix < 5)
                    dly = swing_delay(beat, ctx.current_tempo_interval_ms, pb[ix]);
            }
            uint32_t ton = t + dly;
            if (g_sw_off[pin] == 0 && g_sw_on[pin] == 0) {
                if (dly == 0) {
                    g_io.set_output(pin, true);
                    g_sw_off[pin] = ton + DEFAULT_PULSE_MS;
                } else {
                    g_sw_on[pin] = ton;
                }
            }
        }
    }
}

// --- Polyrhythm ---
static uint32_t g_poly_next[K_NUM_FW_JACKS] = {};
static uint32_t g_poly_off[K_NUM_FW_JACKS] = {};
static const uint8_t g_px_a[4] = {3, 4, 5, 7};
static const uint8_t g_py_a[4] = {2, 2, 3, 4};
static const uint8_t g_px_b[4] = {5, 7, 6, 11};
static const uint8_t g_py_b[4] = {2, 3, 4, 4};

static void mode_poly_reset() {
    std::memset(g_poly_next, 0, sizeof(g_poly_next));
    std::memset(g_poly_off, 0, sizeof(g_poly_off));
    for (int p = J1A; p <= J6B; p++)
        g_io.set_output(p, false);
}

static void mode_poly_init() {
    mode_poly_reset();
}

static void mode_poly_update(const ModeContext& ctx) {
    uint32_t t = ctx.current_time_ms;
    uint32_t tempo = ctx.current_tempo_interval_ms;
    for (int pin = J1A; pin <= J6B; pin++) {
        if (g_poly_off[pin] != 0 && t >= g_poly_off[pin]) {
            g_io.set_output(pin, false);
            g_poly_off[pin] = 0;
        }
    }
    if (ctx.f1_rising_edge) {
        if (g_poly_off[J1A] == 0) {
            g_io.set_output(J1A, true);
            g_poly_off[J1A] = t + DEFAULT_PULSE_MS;
        }
        if (g_poly_off[J1B] == 0) {
            g_io.set_output(J1B, true);
            g_poly_off[J1B] = t + DEFAULT_PULSE_MS;
        }
    }
    const uint8_t *xa, *ya, *xb, *yb;
    if (ctx.calc_mode == CalcMode::Normal) {
        xa = g_px_a;
        ya = g_py_a;
        xb = g_px_b;
        yb = g_py_b;
    } else {
        xa = g_px_b;
        ya = g_py_b;
        xb = g_px_a;
        yb = g_py_a;
    }
    bool t6a = false, t6b = false;
    for (int pin = J2A; pin <= J5A; pin++) {
        int ix = pin - J2A;
        uint8_t X = xa[ix], Y = ya[ix];
        if (X == 0)
            continue;
        uint32_t oi = (uint32_t)Y * tempo / X;
        if (oi == 0)
            oi = 1;
        if (g_poly_next[pin] == 0 || t >= g_poly_next[pin]) {
            if (g_poly_off[pin] == 0) {
                g_io.set_output(pin, true);
                g_poly_off[pin] = t + DEFAULT_PULSE_MS;
                t6a = true;
                uint32_t sch = (g_poly_next[pin] == 0) ? t : g_poly_next[pin];
                g_poly_next[pin] = sch + oi;
            } else {
                if (g_poly_next[pin] != 0)
                    g_poly_next[pin] += oi;
                else
                    g_poly_next[pin] = t + oi;
            }
        }
    }
    for (int pin = J2B; pin <= J5B; pin++) {
        int ix = pin - J2B;
        uint8_t X = xb[ix], Y = yb[ix];
        if (X == 0)
            continue;
        uint32_t oi = (uint32_t)Y * tempo / X;
        if (oi == 0)
            oi = 1;
        if (g_poly_next[pin] == 0 || t >= g_poly_next[pin]) {
            if (g_poly_off[pin] == 0) {
                g_io.set_output(pin, true);
                g_poly_off[pin] = t + DEFAULT_PULSE_MS;
                t6b = true;
                uint32_t sch = (g_poly_next[pin] == 0) ? t : g_poly_next[pin];
                g_poly_next[pin] = sch + oi;
            } else {
                if (g_poly_next[pin] != 0)
                    g_poly_next[pin] += oi;
                else
                    g_poly_next[pin] = t + oi;
            }
        }
    }
    if (t6a && g_poly_off[J6A] == 0) {
        g_io.set_output(J6A, true);
        g_poly_off[J6A] = t + DEFAULT_PULSE_MS;
    }
    if (t6b && g_poly_off[J6B] == 0) {
        g_io.set_output(J6B, true);
        g_poly_off[J6B] = t + DEFAULT_PULSE_MS;
    }
}

// --- Logic ---
static const float g_lg_fa[6] = {1, 2, 4, 0.5f, 0.25f, 3};
static const float g_lg_fb[6] = {1, 0.5f, 0.25f, 2, 4, 6};
static const int g_lg_ja[6] = {J1A, J2A, J3A, J4A, J5A, J6A};
static const int g_lg_jb[6] = {J1B, J2B, J3B, J4B, J5B, J6B};
static bool g_lg_pa[6] = {};
static bool g_lg_pb[6] = {};

static bool logic_is_on(uint32_t iv, uint32_t t, float f) {
    if (f == 0.f)
        return false;
    uint32_t p = (uint32_t)((float)iv / f);
    if (p == 0)
        return true;
    return (t % p) < (p / 2);
}

static void mode_logic_reset() {
    for (int i = 1; i < 6; i++) {
        g_io.set_output(g_lg_ja[i], false);
        g_io.set_output(g_lg_jb[i], false);
        g_lg_pa[i] = g_lg_pb[i] = false;
    }
}

static void mode_logic_init() {
    std::memset(g_lg_pa, 0, sizeof(g_lg_pa));
    std::memset(g_lg_pb, 0, sizeof(g_lg_pb));
}

static void mode_logic_update(const ModeContext& ctx) {
    if (!ctx.f1_rising_edge)
        return;
    uint32_t iv = ctx.current_tempo_interval_ms;
    uint32_t t = ctx.current_time_ms;
    for (int i = 1; i < 6; i++) {
        bool ia = logic_is_on(iv, t, g_lg_fa[i]);
        bool ib = logic_is_on(iv, t, g_lg_fb[i]);
        bool x = ia ^ ib;
        bool n = !(ia || ib);
        bool ca = (ctx.calc_mode == CalcMode::Normal) ? x : n;
        bool cb = (ctx.calc_mode == CalcMode::Normal) ? n : x;
        if (ca && !g_lg_pa[i])
            g_io.pulse_ms(g_lg_ja[i], DEFAULT_PULSE_MS);
        if (cb && !g_lg_pb[i])
            g_io.pulse_ms(g_lg_jb[i], DEFAULT_PULSE_MS);
        g_lg_pa[i] = ca;
        g_lg_pb[i] = cb;
    }
}

// --- Phasing ---
struct PhasingSt {
    uint32_t ms_ctr = 0;
    uint32_t pulse_rem = 0;
};
static PhasingSt g_ph_a[5];
static PhasingSt g_ph_b[5];
static uint8_t g_ph_delta = 0;
static const int g_ph_pa[5] = {J2A, J3A, J4A, J5A, J6A};
static const int g_ph_pb[5] = {J2B, J3B, J4B, J5B, J6B};
static const uint16_t g_ph_fac[5][2] = {{1, 1}, {1, 2}, {2, 1}, {1, 3}, {3, 1}};
static const float g_ph_df[3] = {0.1f, 1.f, 5.f};

static uint32_t ph_derived(uint32_t base, uint16_t mul, uint16_t div) {
    if (base == 0 || base == UINT32_MAX || div == 0)
        return UINT32_MAX;
    uint64_t r = (uint64_t)base * mul / div;
    if (r > MAX_INTERVAL)
        return MAX_INTERVAL;
    if (r < MIN_INTERVAL)
        return MIN_INTERVAL;
    return (uint32_t)r;
}

static void ph_upd(int pin, PhasingSt* st, uint32_t tgt, uint32_t pw, uint32_t ms_elapsed) {
    if (tgt == UINT32_MAX || tgt == 0) {
        if (st->pulse_rem > 0) {
            st->pulse_rem = 0;
            g_io.set_output(pin, false);
        }
        st->ms_ctr = 0;
        return;
    }
    if (st->pulse_rem > 0) {
        if (ms_elapsed >= st->pulse_rem) {
            st->pulse_rem = 0;
            g_io.set_output(pin, false);
        } else {
            st->pulse_rem -= ms_elapsed;
        }
    }
    st->ms_ctr += ms_elapsed;
    if (st->ms_ctr >= tgt) {
        if (st->pulse_rem == 0) {
            g_io.set_output(pin, true);
            uint32_t ap = (pw >= tgt && tgt > 0) ? tgt - 1 : pw;
            if (ap == 0)
                ap = 1;
            st->pulse_rem = ap;
        }
        st->ms_ctr -= tgt;
    }
}

static void mode_phasing_reset() {
    for (auto& s : g_ph_a)
        s = PhasingSt{};
    for (auto& s : g_ph_b)
        s = PhasingSt{};
    for (int i = 0; i < 5; i++) {
        g_io.set_output(g_ph_pa[i], false);
        g_io.set_output(g_ph_pb[i], false);
    }
}

static void mode_phasing_init() {
    g_ph_delta = 0;
    mode_phasing_reset();
}

static void mode_phasing_update(const ModeContext& ctx) {
    if (ctx.calc_mode_changed)
        g_ph_delta = (g_ph_delta + 1) % 3;
    if (ctx.sync_request)
        mode_phasing_reset();
    uint32_t ba = 0, bb = UINT32_MAX;
    if (ctx.current_tempo_interval_ms != 0) {
        ba = ctx.current_tempo_interval_ms;
        if (ba < MIN_INTERVAL)
            ba = MIN_INTERVAL;
        if (ba > MAX_INTERVAL)
            ba = MAX_INTERVAL;
        float fa = 60000.f / (float)ctx.current_tempo_interval_ms;
        float fb = fa + g_ph_df[g_ph_delta % 3];
        if (fb <= 0.f)
            bb = UINT32_MAX;
        else
            bb = (uint32_t)(60000.f / fb);
        if (bb < MIN_INTERVAL)
            bb = MIN_INTERVAL;
        if (bb > MAX_INTERVAL)
            bb = MAX_INTERVAL;
    }
    uint32_t ms_el = ctx.ms_since_last_call;
    for (int i = 0; i < 5; i++) {
        uint32_t ta = ph_derived(ba, g_ph_fac[i][0], g_ph_fac[i][1]);
        uint32_t tb = ph_derived(bb, g_ph_fac[i][0], g_ph_fac[i][1]);
        ph_upd(g_ph_pa[i], &g_ph_a[i], ta, DEFAULT_PULSE_MS, ms_el);
        ph_upd(g_ph_pb[i], &g_ph_b[i], tb, DEFAULT_PULSE_MS, ms_el);
    }
}

// --- Chaos ---
static float g_lx = 0.1f, g_ly = 0.f, g_lz = 0.f;
static float g_px = 0.1f, g_py = 0.f, g_pz = 0.f;
static uint32_t g_ch_trig[K_NUM_FW_JACKS] = {};
static uint32_t g_ch_xc[5] = {};
static uint32_t g_ch_yc[5] = {};
static const float g_ch_xt[5] = {5, 10, 15, -5, -10};
static const float g_ch_yt[5] = {10, 20, -10, 30, 10};
static const bool g_ch_usey[5] = {true, false, true, false, false};

static bool chaos_cross(float c, float p, float th) {
    if (p < th && c >= th)
        return true;
    if (th < 0 && p > th && c <= th)
        return true;
    if (th >= 0 && p > th && c <= th)
        return true;
    return false;
}

static void mode_chaos_reset() {
    for (int i = J2A; i <= J6B; i++) {
        if ((i <= J6A) || (i >= J2B && i <= J6B)) {
            g_io.set_output(i, false);
            g_ch_trig[i] = 0;
        }
    }
    for (int j = 0; j < 5; j++)
        g_ch_xc[j] = g_ch_yc[j] = 0;
}

static void mode_chaos_init() {
    g_lx = 0.1f;
    g_ly = g_lz = 0.f;
    g_px = g_lx;
    g_py = g_ly;
    g_pz = g_lz;
    for (int i = J1A; i <= J6B; i++) {
        if ((i <= J6A) || (i >= J1B && i <= J6B)) {
            if (i > J6A && !(i >= J1B && i <= J6B))
                continue;
            g_ch_trig[i] = 0;
            g_io.set_output(i, false);
        }
    }
    for (int j = 0; j < 5; j++)
        g_ch_xc[j] = g_ch_yc[j] = 0;
    if (g_eng)
        chaos_validate_divisor(g_eng->chaos_divisor);
}

static void mode_chaos_update(const ModeContext& ctx) {
    if (!g_eng)
        return;
    uint32_t& div = g_eng->chaos_divisor;
    uint32_t t = ctx.current_time_ms;
    for (int i = J2A; i <= J6B; i++) {
        if (i == J1A || i == J1B)
            continue;
        if (i > J6A && !(i >= J2B && i <= J6B))
            continue;
        if (g_ch_trig[i] != 0 && t - g_ch_trig[i] >= DEFAULT_PULSE_MS) {
            g_io.set_output(i, false);
            g_ch_trig[i] = 0;
        }
    }
    if (ctx.calc_mode_changed) {
        if (div <= CHAOS_DIVISOR_MIN)
            div = CHAOS_DIVISOR_DEFAULT;
        else {
            if (div > CHAOS_DIVISOR_STEP)
                div -= CHAOS_DIVISOR_STEP;
            if (div < CHAOS_DIVISOR_MIN)
                div = CHAOS_DIVISOR_MIN;
        }
    }
    uint32_t el = ctx.ms_since_last_call;
    if (el == 0)
        el = 1;
    if (el > 100)
        el = 100;
    int nstep = (int)((float)el / (0.01f * 1000.f));
    if (nstep < 1)
        nstep = 1;
    g_px = g_lx;
    g_py = g_ly;
    g_pz = g_lz;
    const float sg = 10.f, rh = 28.f, bt = 2.666f;
    for (int s = 0; s < nstep; s++) {
        float dx = sg * (g_ly - g_lx);
        float dy = g_lx * (rh - g_lz) - g_ly;
        float dz = g_lx * g_ly - bt * g_lz;
        g_lx += dx * 0.01f;
        g_ly += dy * 0.01f;
        g_lz += dz * 0.01f;
    }
    for (int i = 0; i < 5; i++) {
        int oa = J2A + i;
        if (chaos_cross(g_lx, g_px, g_ch_xt[i])) {
            g_ch_xc[i]++;
            if (g_ch_trig[oa] == 0 && (g_ch_xc[i] % div) == 0) {
                g_io.set_output(oa, true);
                g_ch_trig[oa] = t;
            }
        }
    }
    for (int i = 0; i < 5; i++) {
        int ob = J2B + i;
        float cv = g_ch_usey[i] ? g_ly : g_lz;
        float pv = g_ch_usey[i] ? g_py : g_pz;
        if (chaos_cross(cv, pv, g_ch_yt[i])) {
            g_ch_yc[i]++;
            if (g_ch_trig[ob] == 0 && (g_ch_yc[i] % div) == 0) {
                g_io.set_output(ob, true);
                g_ch_trig[ob] = t;
            }
        }
    }
}

// --- Binary ---
static uint8_t g_bin_step = 0;
static uint8_t g_bin_bank = 0;
static bool g_bin_pend = false;
static uint8_t g_bin_pbank = 0;
static uint32_t g_bin_next = 0;

static void mode_binary_reset() {
    g_bin_step = 0;
    g_bin_bank = 0;
    g_bin_pend = false;
    g_bin_pbank = 0;
    g_bin_next = 0;
    for (int i = J2A; i <= J6B; i++)
        g_io.set_output(i, false);
}

static void mode_binary_init() {
    mode_binary_reset();
}

static void mode_binary_update(const ModeContext& ctx) {
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (g_bin_next == 0)
        g_bin_next = now;
    if (now >= g_bin_next) {
        g_bin_next += si;
        if (g_bin_next < now)
            g_bin_next = now + si;
        uint8_t bank = g_bin_bank;
        for (int i = 0; i < K_NUM_FW_JACKS; i++) {
            uint16_t pat = kBinaryPatterns[bank % kBinaryNumBanks][i];
            if ((pat >> g_bin_step) & 1)
                g_io.pulse_ms(i, DEFAULT_PULSE_MS);
        }
        g_bin_step++;
        if (g_bin_step >= 16) {
            g_bin_step = 0;
            if (g_bin_pend) {
                g_bin_bank = g_bin_pbank;
                g_bin_pend = false;
            }
        }
    }
}

static void mode_binary_set_bank(uint8_t b) {
    if (b < NUM_BINARY_BANKS)
        g_bin_bank = b;
}

static void mode_binary_bank_pending(uint8_t b) {
    if (b < NUM_BINARY_BANKS) {
        g_bin_pbank = b;
        g_bin_pend = true;
    }
}

typedef void (*ModeInitFn)();
typedef void (*ModeResetFn)();
typedef void (*ModeUpdFn)(const ModeContext&);

static void upd_default(const ModeContext& c) {
    mode_default_update(c);
}
static void upd_euc(const ModeContext& c) {
    mode_euclidean_update(c);
}
static void upd_mus(const ModeContext& c) {
    mode_musical_update(c);
}
static void upd_pr(const ModeContext& c) {
    mode_prob_update(c);
}
static void upd_seq(const ModeContext& c) {
    mode_seq_update(c);
}
static void upd_log(const ModeContext& c) {
    mode_logic_update(c);
}

static ModeInitFn g_inits[(int)OpMode::Count] = {};
static ModeResetFn g_resets[(int)OpMode::Count] = {};
static ModeUpdFn g_upds[(int)OpMode::Count] = {};

#include "krono_rhythm_impl.inc"

static void dispatch_init(OpMode m) {
    int i = (int)m;
    if (i >= 0 && i < (int)OpMode::Count && g_inits[i])
        g_inits[i]();
}
static void dispatch_reset(OpMode m) {
    int i = (int)m;
    if (i >= 0 && i < (int)OpMode::Count && g_resets[i])
        g_resets[i]();
}
static void dispatch_upd(OpMode m, const ModeContext& c, HwEngine* eng) {
    switch (m) {
        case OpMode::Swing:
            mode_swing_update(c, eng->swing_profile_a, eng->swing_profile_b);
            break;
        case OpMode::Polyrhythm:
            mode_poly_update(c);
            break;
        case OpMode::Phasing:
            mode_phasing_update(c);
            break;
        case OpMode::Chaos:
            mode_chaos_update(c);
            break;
        case OpMode::Fixed:
            mode_binary_update(c);
            break;
        case OpMode::Drift:
        case OpMode::Fill:
        case OpMode::Skip:
        case OpMode::Stutter:
        case OpMode::Morph:
        case OpMode::Mute:
        case OpMode::Density:
        case OpMode::Song:
        case OpMode::Accumulate:
            rhythm_update_mode(m, c);
            break;
        default: {
            int i = (int)m;
            if (i >= 0 && i < (int)OpMode::Count && g_upds[i])
                g_upds[i](c);
        } break;
    }
}

struct InitTable {
    InitTable() {
        g_inits[(int)OpMode::Default] = mode_default_init;
        g_resets[(int)OpMode::Default] = mode_default_reset;
        g_upds[(int)OpMode::Default] = upd_default;

        g_inits[(int)OpMode::Euclidean] = mode_euclidean_init;
        g_resets[(int)OpMode::Euclidean] = mode_euclidean_reset;
        g_upds[(int)OpMode::Euclidean] = upd_euc;

        g_inits[(int)OpMode::Musical] = mode_musical_init;
        g_resets[(int)OpMode::Musical] = mode_musical_reset;
        g_upds[(int)OpMode::Musical] = upd_mus;

        g_inits[(int)OpMode::Probabilistic] = mode_prob_init;
        g_resets[(int)OpMode::Probabilistic] = mode_prob_reset;
        g_upds[(int)OpMode::Probabilistic] = upd_pr;

        g_inits[(int)OpMode::Sequential] = mode_seq_init;
        g_resets[(int)OpMode::Sequential] = mode_seq_reset;
        g_upds[(int)OpMode::Sequential] = upd_seq;

        g_inits[(int)OpMode::Swing] = mode_swing_init;
        g_resets[(int)OpMode::Swing] = mode_swing_reset;
        g_upds[(int)OpMode::Swing] = nullptr;

        g_inits[(int)OpMode::Polyrhythm] = mode_poly_init;
        g_resets[(int)OpMode::Polyrhythm] = mode_poly_reset;
        g_upds[(int)OpMode::Polyrhythm] = nullptr;

        g_inits[(int)OpMode::Logic] = mode_logic_init;
        g_resets[(int)OpMode::Logic] = mode_logic_reset;
        g_upds[(int)OpMode::Logic] = upd_log;

        g_inits[(int)OpMode::Phasing] = mode_phasing_init;
        g_resets[(int)OpMode::Phasing] = mode_phasing_reset;
        g_upds[(int)OpMode::Phasing] = nullptr;

        g_inits[(int)OpMode::Chaos] = mode_chaos_init;
        g_resets[(int)OpMode::Chaos] = mode_chaos_reset;
        g_upds[(int)OpMode::Chaos] = nullptr;

        g_inits[(int)OpMode::Fixed] = mode_binary_init;
        g_resets[(int)OpMode::Fixed] = mode_binary_reset;
        g_upds[(int)OpMode::Fixed] = nullptr;

        g_inits[(int)OpMode::Drift] = [] { rhythm_init_mode(OpMode::Drift); };
        g_resets[(int)OpMode::Drift] = [] { rhythm_reset_mode(OpMode::Drift); };
        g_upds[(int)OpMode::Drift] = nullptr;

        g_inits[(int)OpMode::Fill] = [] { rhythm_init_mode(OpMode::Fill); };
        g_resets[(int)OpMode::Fill] = [] { rhythm_reset_mode(OpMode::Fill); };
        g_upds[(int)OpMode::Fill] = nullptr;

        g_inits[(int)OpMode::Skip] = [] { rhythm_init_mode(OpMode::Skip); };
        g_resets[(int)OpMode::Skip] = [] { rhythm_reset_mode(OpMode::Skip); };
        g_upds[(int)OpMode::Skip] = nullptr;

        g_inits[(int)OpMode::Stutter] = [] { rhythm_init_mode(OpMode::Stutter); };
        g_resets[(int)OpMode::Stutter] = [] { rhythm_reset_mode(OpMode::Stutter); };
        g_upds[(int)OpMode::Stutter] = nullptr;

        g_inits[(int)OpMode::Morph] = [] { rhythm_init_mode(OpMode::Morph); };
        g_resets[(int)OpMode::Morph] = [] { rhythm_reset_mode(OpMode::Morph); };
        g_upds[(int)OpMode::Morph] = nullptr;

        g_inits[(int)OpMode::Mute] = [] { rhythm_init_mode(OpMode::Mute); };
        g_resets[(int)OpMode::Mute] = [] { rhythm_reset_mode(OpMode::Mute); };
        g_upds[(int)OpMode::Mute] = nullptr;

        g_inits[(int)OpMode::Density] = [] { rhythm_init_mode(OpMode::Density); };
        g_resets[(int)OpMode::Density] = [] { rhythm_reset_mode(OpMode::Density); };
        g_upds[(int)OpMode::Density] = nullptr;

        g_inits[(int)OpMode::Song] = [] { rhythm_init_mode(OpMode::Song); };
        g_resets[(int)OpMode::Song] = [] { rhythm_reset_mode(OpMode::Song); };
        g_upds[(int)OpMode::Song] = nullptr;

        g_inits[(int)OpMode::Accumulate] = [] { rhythm_init_mode(OpMode::Accumulate); };
        g_resets[(int)OpMode::Accumulate] = [] { rhythm_reset_mode(OpMode::Accumulate); };
        g_upds[(int)OpMode::Accumulate] = nullptr;

        rhythm_register_persist_hooks_fn();
    }
};
static InitTable g_init_table;

} // namespace

HwEngine::HwEngine() {
    on_reset();
}

void HwEngine::on_reset() {
    g_eng = this;
    wall_ms = 0;
    last_engine_ms = 0;
    bypass_first_update = false;
    active_tempo_interval_ms = 60000 / 70;
    last_f1_pulse_time_ms = 0;
    last_update_time_ms = 0;
    f1_tick_counter = 0;
    sync_requested = false;
    calc_mode_just_changed = false;
    g_ext_active = false;
    g_ext_last_rise_ms = g_ext_last_isr_ms = 0;
    g_ext_validated_iv = 0;
    reset_tap_calc();
    reset_ext_validation();
    rhythm_persist = RhythmPersistState{};
    for (int i = 0; i < (int)OpMode::Count; i++)
        calc_mode_per_op[i] = CalcMode::Normal;
    swing_profile_a = swing_profile_b = 3;
    chaos_divisor = CHAOS_DIVISOR_DEFAULT;
    binary_bank = 0;
    calc_mode = CalcMode::Normal;
    op_mode = OpMode::Default;
    g_io.clear();
    dispatch_reset(OpMode::Default);
    dispatch_init(OpMode::Default);
}

void HwEngine::set_op_mode(OpMode m) {
    if (m < OpMode::Default || m >= OpMode::Count)
        m = OpMode::Default;
    if (m == op_mode)
        return;
    g_eng = this;
    g_io.clear();
    dispatch_reset(op_mode);
    op_mode = m;
    calc_mode = calc_mode_per_op[(int)op_mode];
    sync_requested = true;
    calc_mode_just_changed = false;
    f1_tick_counter = 0;
    bypass_first_update = (op_mode == OpMode::Musical || op_mode == OpMode::Polyrhythm);
    dispatch_init(op_mode);
    if (op_mode == OpMode::Fixed)
        mode_binary_set_bank(binary_bank);
    if (op_mode == OpMode::Chaos) {
        if (chaos_divisor < 10 || chaos_divisor > 1000 || (chaos_divisor % 50 != 0))
            chaos_divisor = 1000;
    }
    if (g_hw_rhythm_overlay)
        g_hw_rhythm_overlay(&rhythm_persist, op_mode);
}

void HwEngine::advance_fixed_bank() {
    g_eng = this;
    if (op_mode != OpMode::Fixed)
        return;
    uint8_t nb = (uint8_t)((binary_bank + 1u) % NUM_BINARY_BANKS);
    mode_binary_bank_pending(nb);
}

void HwEngine::rhythm_mod_press(uint32_t now_ms) {
    if (g_hw_rhythm_mod)
        g_hw_rhythm_mod(op_mode, now_ms);
}

void HwEngine::rhythm_snapshot_persist() {
    if (g_hw_rhythm_snapshot)
        g_hw_rhythm_snapshot(&rhythm_persist);
}

void HwEngine::rhythm_apply_persist() {
    if (g_hw_rhythm_apply)
        g_hw_rhythm_apply(&rhythm_persist);
}

void HwEngine::toggle_calc_mode() {
    calc_mode = (calc_mode == CalcMode::Normal) ? CalcMode::Swapped : CalcMode::Normal;
    calc_mode_per_op[(int)op_mode] = calc_mode;
    sync_requested = true;
    calc_mode_just_changed = true;
    f1_tick_counter = 0;
}

void HwEngine::set_tempo_ms(uint32_t interval_ms, bool is_external, uint32_t event_ms) {
    if (interval_ms == 0)
        return;
    active_tempo_interval_ms = interval_ms;
    if (is_external) {
        g_ext_active = true;
        last_f1_pulse_time_ms = event_ms;
        f1_tick_counter = 0;
        g_io.pulse_ms(J1A, DEFAULT_PULSE_MS);
        g_io.pulse_ms(J1B, DEFAULT_PULSE_MS);
    } else {
        last_f1_pulse_time_ms = event_ms;
        f1_tick_counter = 0;
        g_io.pulse_ms(J1A, DEFAULT_PULSE_MS);
        g_io.pulse_ms(J1B, DEFAULT_PULSE_MS);
    }
}

void HwEngine::resync_to_event(uint32_t event_ms) {
    last_f1_pulse_time_ms = event_ms;
    f1_tick_counter = 0;
    sync_requested = true;
    calc_mode_just_changed = false;
    g_io.clear();
    dispatch_reset(op_mode);
    dispatch_init(op_mode);
    if (op_mode == OpMode::Fixed)
        mode_binary_set_bank(binary_bank);
    if (g_hw_rhythm_overlay)
        g_hw_rhythm_overlay(&rhythm_persist, op_mode);
}

void HwEngine::set_bpm(float bpm) {
    float lo = 60000.f / (float)MAX_INTERVAL;
    float hi = 60000.f / (float)MIN_INTERVAL;
    if (bpm < lo)
        bpm = lo;
    if (bpm > hi)
        bpm = hi;
    active_tempo_interval_ms = (uint32_t)(60000.f / bpm + 0.5f);
}

void HwEngine::on_external_validated(uint32_t interval_ms, uint32_t event_ms) {
    set_tempo_ms(interval_ms, true, event_ms);
}

void HwEngine::on_external_timeout() {
    g_ext_active = false;
}

void HwEngine::process(float st) {
    g_eng = this;
    wall_ms += (double)st * 1000.0;
    uint32_t now_ms = (uint32_t)wall_ms;

    while (last_engine_ms < now_ms) {
        last_engine_ms++;
        uint32_t step_now = last_engine_ms;
        uint32_t ms_delta = step_now - last_update_time_ms;
        last_update_time_ms = step_now;

        bool f1_edge = false;
        if (step_now - last_f1_pulse_time_ms >= active_tempo_interval_ms) {
            g_io.pulse_ms(J1A, DEFAULT_PULSE_MS);
            g_io.pulse_ms(J1B, DEFAULT_PULSE_MS);
            last_f1_pulse_time_ms += active_tempo_interval_ms;
            f1_tick_counter++;
            f1_edge = true;
        }

        ModeContext ctx;
        ctx.current_time_ms = step_now;
        ctx.current_tempo_interval_ms = active_tempo_interval_ms;
        ctx.calc_mode = calc_mode;
        ctx.calc_mode_changed = calc_mode_just_changed;
        ctx.f1_rising_edge = f1_edge;
        ctx.f1_counter = f1_tick_counter;
        ctx.ms_since_last_call = ms_delta;
        ctx.sync_request = sync_requested;
        ctx.bypass_first_update = false;

        if (bypass_first_update) {
            bypass_first_update = false;
        } else {
            dispatch_upd(op_mode, ctx, this);
        }

        sync_requested = false;
        calc_mode_just_changed = false;
    }

    g_io.step_audio(st);
    g_io.to_rack(out_v);
    if (op_mode == OpMode::Fixed)
        binary_bank = g_bin_bank;
}

} // namespace krono
