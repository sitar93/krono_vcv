// Included inside `namespace krono { namespace { ... } }` in krono_hw_engine.cpp
// Port of firmware mode_rhythm_shared + modes 12–20 (drift … accumulate).

static const Jack kRJack[10] = {J2A, J2B, J3A, J3B, J4A, J4B, J5A, J5B, J6A, J6B};

static const uint16_t g_rhythm_base[2][10] = {
    {0b1000100010001000, 0b0000100010001001, 0b0010001000100010, 0b0000001000100010, 0b0010001000100010, 0b0010000000000010,
     0b1010101010101010, 0b0010101010101010, 0b1111111111111111, 0b1111111101111111},
    {0b1000000010000000, 0b1000000000000001, 0b0010000000100010, 0b0000000000100010, 0b0010000100000010, 0b0010000100000000,
     0b1010101010101010, 0b0010101010101010, 0b1100110011001100, 0b1100110011001000}};

static uint16_t r_base_pat(CalcMode calc, int oi) {
    if (oi < 0 || oi >= 10)
        return 0;
    int bank = (calc == CalcMode::Swapped) ? 1 : 0;
    return g_rhythm_base[bank][oi];
}

static void r_pulse_oi(int oi, uint32_t ms) {
    if (oi >= 0 && oi < 10)
        g_io.pulse_ms(kRJack[oi], ms);
}

static void r_all_low() {
    for (int i = 0; i < 10; i++)
        g_io.set_output(kRJack[i], false);
}

// --- Drift ---
static uint16_t r_drift_pat[10];
static bool r_drift_active;
static uint8_t r_drift_prob;
static bool r_drift_ramp_up;
static uint8_t r_drift_step;
static uint32_t r_drift_next;
static CalcMode r_drift_calc;

static void r_drift_reload() {
    for (int i = 0; i < 10; i++)
        r_drift_pat[i] = r_base_pat(r_drift_calc, i);
}

static void r_drift_reset() {
    r_drift_active = false;
    r_drift_prob = 0;
    r_drift_ramp_up = true;
    r_drift_step = 0;
    r_drift_next = 0;
    r_drift_reload();
    r_all_low();
}

static void r_drift_init() {
    r_drift_reset();
    r_drift_next = 0;
}

static void r_drift_mod() {
    if (!r_drift_active) {
        r_drift_active = true;
        r_drift_prob = 10;
        r_drift_ramp_up = true;
    } else if (r_drift_ramp_up) {
        if (r_drift_prob >= 100) {
            r_drift_ramp_up = false;
            r_drift_prob = 90;
        } else {
            r_drift_prob = (uint8_t)(r_drift_prob + 10);
        }
    } else {
        if (r_drift_prob == 0) {
            r_drift_ramp_up = true;
            r_drift_prob = 10;
        } else {
            r_drift_prob = (uint8_t)(r_drift_prob - 10);
        }
    }
}

static void r_drift_upd(const ModeContext& ctx) {
    r_drift_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_drift_step = 0;
    if (ctx.calc_mode_changed)
        r_drift_reload();
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_drift_next == 0)
        r_drift_next = now;
    if (now < r_drift_next)
        return;
    r_drift_next += si;
    if (r_drift_next < now)
        r_drift_next = now + si;
    if (r_drift_step == 0 && r_drift_active && r_drift_prob > 0) {
        uint8_t mutations = (uint8_t)(1u + (std::rand() % 3));
        for (uint8_t m = 0; m < mutations; m++) {
            if ((uint8_t)(std::rand() % 100) < r_drift_prob) {
                int oi = std::rand() % 10;
                int bi = std::rand() % 16;
                r_drift_pat[oi] ^= (uint16_t)(1u << bi);
            }
        }
        if ((uint8_t)(std::rand() % 100) < (uint8_t)(r_drift_prob / 2u)) {
            int oi = std::rand() % 10;
            int bj = std::rand() % 16;
            int bk = std::rand() % 16;
            r_drift_pat[oi] ^= (uint16_t)((1u << bj) | (1u << bk));
        }
    }
    for (int i = 0; i < 10; i++) {
        if ((r_drift_pat[i] >> r_drift_step) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_drift_step++;
    if (r_drift_step >= 16)
        r_drift_step = 0;
}

// --- Fill ---
static uint8_t r_fill_density;
static bool r_fill_ramp_up;
static uint16_t r_fill_mask[10];
static uint8_t r_fill_step;
static uint32_t r_fill_next;
static CalcMode r_fill_calc;

static void r_fill_regen() {
    static const uint8_t keep_probs[6] = {0, 8, 22, 40, 62, 85};
    static const uint8_t add_probs[6] = {0, 4, 12, 24, 38, 56};
    uint8_t stage = (uint8_t)(r_fill_density / 10u);
    if (stage > 5u)
        stage = 5u;
    for (int oi = 0; oi < 10; oi++) {
        uint16_t base = r_base_pat(r_fill_calc, oi);
        uint16_t m = 0;
        if (oi <= 1) {
            m = base;
        } else if (stage > 0) {
            uint8_t keep_prob = keep_probs[stage];
            uint8_t add_prob = add_probs[stage];
            for (int b = 0; b < 16; b++) {
                bool base_on = ((base >> b) & 1) != 0;
                if (base_on) {
                    if ((uint8_t)(std::rand() % 100) < keep_prob)
                        m |= (uint16_t)(1u << b);
                } else if ((uint8_t)(std::rand() % 100) < add_prob) {
                    m |= (uint16_t)(1u << b);
                }
            }
        }
        r_fill_mask[oi] = m;
    }
}

static void r_fill_reset() {
    r_fill_density = 0;
    r_fill_ramp_up = true;
    r_fill_step = 0;
    r_fill_next = 0;
    for (int i = 0; i < 10; i++)
        r_fill_mask[i] = 0;
    r_all_low();
}

static void r_fill_init() {
    r_fill_reset();
    r_fill_next = 0;
}

static void r_fill_mod() {
    if (r_fill_density >= 50)
        r_fill_density = 0;
    else
        r_fill_density = (uint8_t)(r_fill_density + 10);
    r_fill_ramp_up = true;
}

static void r_fill_upd(const ModeContext& ctx) {
    r_fill_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_fill_step = 0;
    if (ctx.calc_mode_changed)
        r_fill_regen();
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_fill_next == 0)
        r_fill_next = now;
    if (now < r_fill_next)
        return;
    r_fill_next += si;
    if (r_fill_next < now)
        r_fill_next = now + si;
    if (r_fill_step == 0)
        r_fill_regen();
    for (int i = 0; i < 10; i++) {
        uint16_t base = r_base_pat(r_fill_calc, i);
        uint16_t ex = r_fill_mask[i];
        if (((base >> r_fill_step) & 1) || ((ex >> r_fill_step) & 1))
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_fill_step++;
    if (r_fill_step >= 16)
        r_fill_step = 0;
}

// --- Skip ---
static uint8_t r_skip_prob;
static bool r_skip_active;
static bool r_skip_ramp_up;
static uint8_t r_skip_step;
static uint32_t r_skip_next;
static CalcMode r_skip_calc;

static void r_skip_reset() {
    r_skip_active = false;
    r_skip_prob = 0;
    r_skip_ramp_up = true;
    r_skip_step = 0;
    r_skip_next = 0;
    r_all_low();
}

static void r_skip_init() {
    r_skip_reset();
    r_skip_next = 0;
}

static void r_skip_mod() {
    if (!r_skip_active) {
        r_skip_active = true;
        r_skip_prob = 10;
        r_skip_ramp_up = true;
    } else if (r_skip_ramp_up) {
        if (r_skip_prob >= 100) {
            r_skip_ramp_up = false;
            r_skip_prob = 90;
        } else {
            r_skip_prob = (uint8_t)(r_skip_prob + 10);
        }
    } else {
        if (r_skip_prob == 0) {
            r_skip_ramp_up = true;
            r_skip_prob = 10;
        } else {
            r_skip_prob = (uint8_t)(r_skip_prob - 10);
        }
    }
}

static void r_skip_upd(const ModeContext& ctx) {
    r_skip_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_skip_step = 0;
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_skip_next == 0)
        r_skip_next = now;
    if (now < r_skip_next)
        return;
    r_skip_next += si;
    if (r_skip_next < now)
        r_skip_next = now + si;
    for (int i = 0; i < 10; i++) {
        uint16_t base = r_base_pat(r_skip_calc, i);
        if ((base >> r_skip_step) & 1) {
            bool play = true;
            if (r_skip_active && r_skip_prob > 0 && (uint8_t)(std::rand() % 100) < r_skip_prob)
                play = false;
            if (play)
                r_pulse_oi(i, DEFAULT_PULSE_MS);
        }
    }
    r_skip_step++;
    if (r_skip_step >= 16)
        r_skip_step = 0;
}

// --- Stutter ---
static bool r_st_active;
static uint8_t r_st_len;
static bool r_st_ramp_up;
static uint16_t r_st_var[10];
static uint8_t r_st_seq;
static uint8_t r_st_step;
static uint32_t r_st_next;
static CalcMode r_st_calc;

static void r_st_reset() {
    r_st_active = false;
    r_st_len = 2;
    r_st_ramp_up = true;
    for (int i = 0; i < 10; i++)
        r_st_var[i] = 0;
    r_st_seq = 0;
    r_st_step = 0;
    r_st_next = 0;
    r_all_low();
}

static void r_st_init() {
    r_st_reset();
    r_st_next = 0;
}

static void r_st_mod() {
    if (!r_st_active) {
        r_st_active = true;
        r_st_len = 2;
    } else if (r_st_len == 2)
        r_st_len = 4;
    else if (r_st_len == 4)
        r_st_len = 8;
    else
        r_st_len = 2;
    r_st_ramp_up = true;
}

static void r_st_upd(const ModeContext& ctx) {
    r_st_calc = ctx.calc_mode;
    if (ctx.sync_request) {
        r_st_step = 0;
        r_st_seq = 0;
    }
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_st_next == 0)
        r_st_next = now;
    if (now < r_st_next)
        return;
    r_st_next += si;
    if (r_st_next < now)
        r_st_next = now + si;
    uint8_t bit_pos = r_st_active ? r_st_seq : r_st_step;
    for (int i = 0; i < 10; i++) {
        uint16_t base = r_base_pat(r_st_calc, i) ^ r_st_var[i];
        if ((base >> bit_pos) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    if (r_st_active && r_st_len >= 2) {
        r_st_seq++;
        if (r_st_seq >= r_st_len) {
            r_st_seq = 0;
            int oi = std::rand() % 10;
            int b1 = std::rand() % 16;
            int b2 = std::rand() % 16;
            r_st_var[oi] ^= (uint16_t)((1u << b1) | (1u << b2));
        }
    } else {
        r_st_step++;
        if (r_st_step >= 16)
            r_st_step = 0;
    }
}

// --- Morph ---
static uint16_t r_mp_a[10], r_mp_b[10], r_mp_m[10];
static bool r_mp_frozen;
static uint8_t r_mp_step;
static uint32_t r_mp_next;
static uint32_t r_mp_gen;
static CalcMode r_mp_calc;

static uint32_t r_mp_rng(uint32_t x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static void r_mp_load_base() {
    for (int i = 0; i < 10; i++) {
        r_mp_a[i] = r_base_pat(r_mp_calc, i);
        r_mp_b[i] = r_mp_a[i];
        r_mp_m[i] = r_mp_a[i];
    }
}

static void r_mp_gen_next() {
    static const uint32_t primes[10] = {2u, 3u, 5u, 7u, 11u, 13u, 17u, 19u, 23u, 29u};
    for (int oi = 0; oi < 10; oi++) {
        uint16_t prev = r_mp_m[oi];
        uint16_t base = r_mp_a[oi];
        uint32_t seed = (r_mp_gen + 1u) * 2654435761u;
        seed ^= primes[oi] * 40503u;
        seed ^= (uint32_t)(prev << (oi % 7));
        uint32_t r = r_mp_rng(seed);
        int flips = 1 + (int)(r % 3u);
        uint16_t next = prev;
        for (int k = 0; k < flips; k++) {
            r = r_mp_rng(r + (uint32_t)(k * 17 + oi));
            uint8_t bit = (uint8_t)(r & 0x0Fu);
            next ^= (uint16_t)(1u << bit);
        }
        r = r_mp_rng(r ^ 0x9E3779B9u);
        uint8_t anchor = (uint8_t)(r & 0x0Fu);
        if ((base >> anchor) & 1u)
            next |= (uint16_t)(1u << anchor);
        else
            next &= (uint16_t)~(1u << anchor);
        r_mp_b[oi] = next;
        r_mp_m[oi] = next;
    }
    r_mp_gen++;
}

static void r_mp_reset() {
    r_mp_frozen = false;
    r_mp_step = 0;
    r_mp_next = 0;
    r_mp_gen = 0;
    r_mp_calc = CalcMode::Normal;
    r_mp_load_base();
    r_mp_gen_next();
    r_all_low();
}

static void r_mp_init() {
    r_mp_reset();
    r_mp_next = 0;
}

static void r_mp_mod() {
    if (r_mp_frozen) {
        r_mp_frozen = false;
        r_mp_gen_next();
    } else {
        r_mp_frozen = true;
    }
}

static void r_mp_upd(const ModeContext& ctx) {
    r_mp_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_mp_step = 0;
    if (ctx.calc_mode_changed)
        r_mp_load_base();
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_mp_next == 0)
        r_mp_next = now;
    if (now < r_mp_next)
        return;
    r_mp_next += si;
    if (r_mp_next < now)
        r_mp_next = now + si;
    if (!r_mp_frozen && r_mp_step == 0)
        r_mp_gen_next();
    for (int i = 0; i < 10; i++) {
        if ((r_mp_m[i] >> r_mp_step) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_mp_step++;
    if (r_mp_step >= 16)
        r_mp_step = 0;
}

// --- Mute ---
static bool r_mu_muted[10];
static uint16_t r_mu_var[10];
static uint8_t r_mu_count;
static bool r_mu_ramp_up;
static uint8_t r_mu_step;
static uint32_t r_mu_next;
static CalcMode r_mu_calc;

static int r_mu_pick(bool want_muted) {
    uint8_t c = 0;
    for (int i = 0; i < 10; i++) {
        if (r_mu_muted[i] == want_muted)
            c++;
    }
    if (c == 0)
        return -1;
    uint8_t pick = (uint8_t)(std::rand() % c);
    for (int i = 0; i < 10; i++) {
        if (r_mu_muted[i] == want_muted) {
            if (pick == 0)
                return i;
            pick--;
        }
    }
    return -1;
}

static void r_mu_reset() {
    std::memset(r_mu_muted, 0, sizeof r_mu_muted);
    std::memset(r_mu_var, 0, sizeof r_mu_var);
    r_mu_count = 0;
    r_mu_ramp_up = true;
    r_mu_step = 0;
    r_mu_next = 0;
    r_all_low();
}

static void r_mu_init() {
    r_mu_reset();
    r_mu_next = 0;
}

static void r_mu_mod() {
    if (r_mu_ramp_up) {
        int idx = r_mu_pick(false);
        if (idx >= 0) {
            r_mu_muted[idx] = true;
            r_mu_count++;
        }
        if (r_mu_count >= 10)
            r_mu_ramp_up = false;
    } else {
        int idx = r_mu_pick(true);
        if (idx >= 0) {
            r_mu_muted[idx] = false;
            uint8_t b1 = (uint8_t)(std::rand() % 16);
            uint8_t b2 = (uint8_t)(std::rand() % 16);
            r_mu_var[idx] ^= (uint16_t)((1u << b1) | (1u << b2));
            if (r_mu_count > 0)
                r_mu_count--;
        }
        if (r_mu_count == 0)
            r_mu_ramp_up = true;
    }
}

static void r_mu_upd(const ModeContext& ctx) {
    r_mu_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_mu_step = 0;
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_mu_next == 0)
        r_mu_next = now;
    if (now < r_mu_next)
        return;
    r_mu_next += si;
    if (r_mu_next < now)
        r_mu_next = now + si;
    for (int i = 0; i < 10; i++) {
        if (r_mu_muted[i])
            continue;
        uint16_t base = r_base_pat(r_mu_calc, i) ^ r_mu_var[i];
        if ((base >> r_mu_step) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_mu_step++;
    if (r_mu_step >= 16)
        r_mu_step = 0;
}

// --- Density ---
static uint16_t r_dn_pat[10];
static uint8_t r_dn_pct;
static uint8_t r_dn_step;
static uint32_t r_dn_next;
static bool r_dn_pending;
static CalcMode r_dn_calc;

static void r_dn_recalc() {
    for (int oi = 0; oi < 10; oi++) {
        uint16_t base = r_base_pat(r_dn_calc, oi);
        uint16_t p = base;
        if (r_dn_pct < 100) {
            uint8_t rem = (uint8_t)(100 - r_dn_pct);
            for (int b = 0; b < 16; b++) {
                if ((p >> b) & 1) {
                    if ((uint8_t)(std::rand() % 100) < rem)
                        p &= (uint16_t)~(1u << b);
                }
            }
        } else if (r_dn_pct > 100) {
            uint8_t add = (uint8_t)((r_dn_pct > 200) ? 100 : (r_dn_pct - 100));
            for (int b = 0; b < 16; b++) {
                if (((p >> b) & 1) == 0) {
                    if ((uint8_t)(std::rand() % 100) < add)
                        p |= (uint16_t)(1u << b);
                }
            }
        } else {
            p = base;
        }
        r_dn_pat[oi] = p;
    }
    r_dn_pending = false;
}

static void r_dn_reset() {
    r_dn_pct = 100;
    r_dn_step = 0;
    r_dn_next = 0;
    r_dn_pending = true;
    for (int i = 0; i < 10; i++)
        r_dn_pat[i] = 0;
    r_all_low();
}

static void r_dn_init() {
    r_dn_reset();
    r_dn_next = 0;
}

static void r_dn_mod() {
    if (r_dn_pct >= 200)
        r_dn_pct = 0;
    else
        r_dn_pct = (uint8_t)(r_dn_pct + 10);
    r_dn_pending = true;
}

static void r_dn_upd(const ModeContext& ctx) {
    r_dn_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_dn_step = 0;
    if (ctx.calc_mode_changed)
        r_dn_pending = true;
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_dn_next == 0)
        r_dn_next = now;
    if (now < r_dn_next)
        return;
    r_dn_next += si;
    if (r_dn_next < now)
        r_dn_next = now + si;
    if (r_dn_step == 0 && r_dn_pending)
        r_dn_recalc();
    for (int i = 0; i < 10; i++) {
        if ((r_dn_pat[i] >> r_dn_step) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_dn_step++;
    if (r_dn_step >= 16)
        r_dn_step = 0;
}

// --- Song ---
static uint16_t r_sg_gen[10], r_sg_var[10];
static uint8_t r_sg_song_step;
static uint8_t r_sg_cur_step;
static uint32_t r_sg_next;
static bool r_sg_var_pend;
static uint32_t r_sg_seed;
static uint32_t r_sg_nonce;
static CalcMode r_sg_calc;

static int r_sg_pop(uint16_t x) {
    int c = 0;
    for (int i = 0; i < 16; i++) {
        if ((x >> i) & 1)
            c++;
    }
    return c;
}

static uint32_t r_sg_lcg(uint32_t* st) {
    *st = *st * 1103515245u + 12345u;
    return *st;
}

static uint16_t r_sg_track(uint32_t* rng, uint16_t base) {
    int bd = r_sg_pop(base);
    uint32_t r = r_sg_lcg(rng);
    int delta = (int)(r % 81u) - 40;
    int target = bd + (bd * delta) / 100;
    if (target < 0)
        target = 0;
    if (target > 16)
        target = 16;
    uint16_t p = 0;
    int positions[16];
    for (int i = 0; i < 16; i++)
        positions[i] = i;
    for (int i = 15; i > 0; i--) {
        uint32_t rv = r_sg_lcg(rng);
        int j = (int)(rv % (uint32_t)(i + 1));
        int tmp = positions[i];
        positions[i] = positions[j];
        positions[j] = tmp;
    }
    for (int k = 0; k < target; k++)
        p |= (uint16_t)(1u << positions[k]);
    return p;
}

static void r_sg_regen(uint32_t seed) {
    uint32_t rng = seed;
    for (int oi = 0; oi < 10; oi++) {
        uint16_t base = r_base_pat(r_sg_calc, oi);
        r_sg_gen[oi] = r_sg_track(&rng, base);
        r_sg_var[oi] = r_sg_track(&rng, r_sg_gen[oi]);
    }
}

static void r_sg_reset() {
    r_sg_song_step = 0;
    r_sg_cur_step = 0;
    r_sg_next = 0;
    r_sg_var_pend = false;
    r_sg_seed = 0xC0FFEE01u;
    r_sg_nonce = 0;
    r_sg_calc = CalcMode::Normal;
    r_sg_regen(r_sg_seed);
    r_all_low();
}

static void r_sg_init() {
    r_sg_reset();
    r_sg_next = 0;
}

static void r_sg_mod(uint32_t ts) {
    r_sg_nonce++;
    r_sg_seed = ts ^ (r_sg_nonce * 2654435761u) ^ 0xA5C3E91Du;
    r_sg_var_pend = true;
}

static void r_sg_upd(const ModeContext& ctx) {
    r_sg_calc = ctx.calc_mode;
    if (ctx.sync_request) {
        r_sg_cur_step = 0;
        r_sg_song_step = 0;
    }
    if (ctx.calc_mode_changed)
        r_sg_regen(r_sg_seed);
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_sg_next == 0)
        r_sg_next = now;
    if (now < r_sg_next)
        return;
    r_sg_next += si;
    if (r_sg_next < now)
        r_sg_next = now + si;
    uint8_t bit_pos = (uint8_t)(r_sg_song_step % 16u);
    for (int i = 0; i < 10; i++) {
        uint16_t pat = (r_sg_song_step < 96) ? r_sg_gen[i] : r_sg_var[i];
        if ((pat >> bit_pos) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_sg_song_step++;
    if (r_sg_song_step >= 128) {
        r_sg_song_step = 0;
        if (r_sg_var_pend) {
            r_sg_regen(r_sg_seed);
            r_sg_var_pend = false;
        }
    }
    r_sg_cur_step = (uint8_t)(r_sg_song_step % 16u);
}

// --- Accumulate ---
static uint8_t r_ac_count;
static uint32_t r_ac_accum_step;
static bool r_ac_add_pend;
static uint8_t r_ac_step;
static uint32_t r_ac_next;
static CalcMode r_ac_calc;
static bool r_ac_flags[10];
static uint8_t r_ac_phase[10];
static uint16_t r_ac_var[10];
static uint8_t r_ac_bars;

static void r_ac_activate_rand() {
    int cand[10];
    int n = 0;
    for (int i = 0; i < 10; i++) {
        if (!r_ac_flags[i])
            cand[n++] = i;
    }
    if (n <= 0)
        return;
    int idx = cand[std::rand() % n];
    r_ac_flags[idx] = true;
    r_ac_phase[idx] = (uint8_t)(std::rand() % 16);
    uint8_t b1 = (uint8_t)(std::rand() % 16);
    uint8_t b2 = (uint8_t)(std::rand() % 16);
    r_ac_var[idx] ^= (uint16_t)((1u << b1) | (1u << b2));
}

static void r_ac_reset_min() {
    for (int i = 0; i < 10; i++) {
        r_ac_flags[i] = false;
        r_ac_var[i] = 0;
    }
    r_ac_count = 1;
    r_ac_bars = 0;
    r_ac_activate_rand();
}

static void r_ac_reset() {
    r_ac_count = 1;
    r_ac_accum_step = 0;
    r_ac_add_pend = false;
    r_ac_step = 0;
    r_ac_next = 0;
    r_ac_bars = 0;
    for (int i = 0; i < 10; i++) {
        r_ac_flags[i] = false;
        r_ac_phase[i] = 0;
        r_ac_var[i] = 0;
    }
    r_ac_activate_rand();
    r_all_low();
}

static void r_ac_init() {
    r_ac_reset();
    r_ac_next = 0;
}

static void r_ac_mod() {
    r_ac_add_pend = !r_ac_add_pend;
}

static void r_ac_upd(const ModeContext& ctx) {
    r_ac_calc = ctx.calc_mode;
    if (ctx.sync_request)
        r_ac_step = 0;
    uint32_t now = ctx.current_time_ms;
    uint32_t si = ctx.current_tempo_interval_ms / 4;
    if (si < 5)
        si = 5;
    if (r_ac_next == 0)
        r_ac_next = now;
    if (now < r_ac_next)
        return;
    r_ac_next += si;
    if (r_ac_next < now)
        r_ac_next = now + si;
    r_ac_accum_step++;
    if (r_ac_step == 0) {
        if (!r_ac_add_pend) {
            r_ac_bars++;
            if (r_ac_bars >= r_ac_count) {
                r_ac_bars = 0;
                if (r_ac_count < 10) {
                    r_ac_count++;
                    r_ac_activate_rand();
                } else {
                    r_ac_reset_min();
                }
            }
        }
    }
    for (int i = 0; i < 10; i++) {
        if (!r_ac_flags[i])
            continue;
        uint16_t base = r_base_pat(r_ac_calc, i) ^ r_ac_var[i];
        uint8_t pos = (uint8_t)((r_ac_step + r_ac_phase[i]) & 0x0Fu);
        if ((base >> pos) & 1)
            r_pulse_oi(i, DEFAULT_PULSE_MS);
    }
    r_ac_step++;
    if (r_ac_step >= 16)
        r_ac_step = 0;
}

// --- Dispatch ---
static void rhythm_reset_mode(OpMode m) {
    switch (m) {
        case OpMode::Drift:
            r_drift_reset();
            break;
        case OpMode::Fill:
            r_fill_reset();
            break;
        case OpMode::Skip:
            r_skip_reset();
            break;
        case OpMode::Stutter:
            r_st_reset();
            break;
        case OpMode::Morph:
            r_mp_reset();
            break;
        case OpMode::Mute:
            r_mu_reset();
            break;
        case OpMode::Density:
            r_dn_reset();
            break;
        case OpMode::Song:
            r_sg_reset();
            break;
        case OpMode::Accumulate:
            r_ac_reset();
            break;
        default:
            break;
    }
}

static void rhythm_init_mode(OpMode m) {
    switch (m) {
        case OpMode::Drift:
            r_drift_init();
            break;
        case OpMode::Fill:
            r_fill_init();
            break;
        case OpMode::Skip:
            r_skip_init();
            break;
        case OpMode::Stutter:
            r_st_init();
            break;
        case OpMode::Morph:
            r_mp_init();
            break;
        case OpMode::Mute:
            r_mu_init();
            break;
        case OpMode::Density:
            r_dn_init();
            break;
        case OpMode::Song:
            r_sg_init();
            break;
        case OpMode::Accumulate:
            r_ac_init();
            break;
        default:
            break;
    }
}

static void rhythm_update_mode(OpMode m, const ModeContext& ctx) {
    switch (m) {
        case OpMode::Drift:
            r_drift_upd(ctx);
            break;
        case OpMode::Fill:
            r_fill_upd(ctx);
            break;
        case OpMode::Skip:
            r_skip_upd(ctx);
            break;
        case OpMode::Stutter:
            r_st_upd(ctx);
            break;
        case OpMode::Morph:
            r_mp_upd(ctx);
            break;
        case OpMode::Mute:
            r_mu_upd(ctx);
            break;
        case OpMode::Density:
            r_dn_upd(ctx);
            break;
        case OpMode::Song:
            r_sg_upd(ctx);
            break;
        case OpMode::Accumulate:
            r_ac_upd(ctx);
            break;
        default:
            break;
    }
}

static void rhythm_mod_mode(OpMode m, uint32_t ts) {
    switch (m) {
        case OpMode::Drift:
            r_drift_mod();
            break;
        case OpMode::Fill:
            r_fill_mod();
            break;
        case OpMode::Skip:
            r_skip_mod();
            break;
        case OpMode::Stutter:
            r_st_mod();
            break;
        case OpMode::Morph:
            r_mp_mod();
            break;
        case OpMode::Mute:
            r_mu_mod();
            break;
        case OpMode::Density:
            r_dn_mod();
            break;
        case OpMode::Song:
            r_sg_mod(ts);
            break;
        case OpMode::Accumulate:
            r_ac_mod();
            break;
        default:
            break;
    }
}

static void rhythm_snapshot_impl(RhythmPersistState* s) {
    s->drift_active = r_drift_active;
    s->drift_probability = r_drift_prob;
    s->drift_ramp_up = r_drift_ramp_up;
    s->fill_density = r_fill_density;
    s->fill_ramp_up = r_fill_ramp_up;
    s->skip_active = r_skip_active;
    s->skip_probability = r_skip_prob;
    s->skip_ramp_up = r_skip_ramp_up;
    s->stutter_active = r_st_active;
    s->stutter_length = r_st_len;
    s->stutter_ramp_up = r_st_ramp_up;
    for (int i = 0; i < 10; i++)
        s->stutter_variation_mask[i] = r_st_var[i];
    s->morph_frozen = r_mp_frozen;
    s->morph_generation = r_mp_gen;
    for (int i = 0; i < 10; i++)
        s->morph_patterns[i] = r_mp_m[i];
    uint16_t mm = 0;
    for (int i = 0; i < 10; i++) {
        if (r_mu_muted[i])
            mm |= (uint16_t)(1u << i);
    }
    s->mute_mask = mm;
    s->mute_count = r_mu_count;
    s->mute_ramp_up = r_mu_ramp_up;
    for (int i = 0; i < 10; i++)
        s->mute_variation_mask[i] = r_mu_var[i];
    s->density_pct = r_dn_pct;
    s->density_ramp_up = true;
    s->song_variation_seed = r_sg_seed;
    s->song_variation_pending = r_sg_var_pend;
    s->accumulate_active_count = r_ac_count;
    s->accumulate_add_pending = r_ac_add_pend;
    uint16_t am = 0;
    for (int i = 0; i < 10; i++) {
        if (r_ac_flags[i])
            am |= (uint16_t)(1u << i);
    }
    s->accumulate_active_mask = am;
    for (int i = 0; i < 10; i++) {
        s->accumulate_phase_offsets[i] = r_ac_phase[i];
        s->accumulate_variation_masks[i] = r_ac_var[i];
    }
}

static void rhythm_apply_impl(const RhythmPersistState* s) {
    r_drift_active = s->drift_active;
    r_drift_prob = s->drift_probability;
    r_drift_ramp_up = s->drift_ramp_up;
    r_fill_density = s->fill_density;
    r_fill_ramp_up = s->fill_ramp_up;
    r_skip_active = s->skip_active;
    r_skip_prob = s->skip_probability;
    r_skip_ramp_up = s->skip_ramp_up;
    r_st_active = s->stutter_active;
    r_st_len = s->stutter_length;
    if (r_st_len != 2 && r_st_len != 4 && r_st_len != 8)
        r_st_len = 2;
    r_st_ramp_up = s->stutter_ramp_up;
    for (int i = 0; i < 10; i++)
        r_st_var[i] = s->stutter_variation_mask[i];
    r_mp_frozen = s->morph_frozen;
    r_mp_gen = s->morph_generation;
    for (int i = 0; i < 10; i++) {
        r_mu_muted[i] = ((s->mute_mask >> i) & 1u) != 0;
        r_mu_var[i] = s->mute_variation_mask[i];
    }
    r_mu_count = s->mute_count;
    if (r_mu_count > 10)
        r_mu_count = 10;
    r_mu_ramp_up = s->mute_ramp_up;
    r_dn_pct = s->density_pct;
    if (r_dn_pct > 200)
        r_dn_pct = 100;
    r_dn_pending = true;
    r_sg_seed = s->song_variation_seed;
    r_sg_var_pend = s->song_variation_pending;
    r_ac_count = s->accumulate_active_count;
    if (r_ac_count < 1 || r_ac_count > 10)
        r_ac_count = 1;
    r_ac_add_pend = s->accumulate_add_pending;
    for (int i = 0; i < 10; i++) {
        r_ac_flags[i] = ((s->accumulate_active_mask >> i) & 1u) != 0;
        r_ac_phase[i] = (uint8_t)(s->accumulate_phase_offsets[i] & 0x0Fu);
        r_ac_var[i] = s->accumulate_variation_masks[i];
    }
    if (s->accumulate_active_mask == 0) {
        r_ac_flags[0] = true;
        r_ac_count = 1;
    }
    r_ac_bars = 0;
    if (g_eng && g_eng->op_mode == OpMode::Morph) {
        r_mp_calc = g_eng->calc_mode;
        r_mp_load_base();
        for (int i = 0; i < 10; i++) {
            r_mp_m[i] = s->morph_patterns[i];
            r_mp_b[i] = r_mp_m[i];
        }
    }
}

static void rhythm_overlay_impl(const RhythmPersistState* p, OpMode m) {
    const int i = (int)m;
    if (i >= 11 && i <= 19)
        rhythm_apply_impl(p);
}

static void rhythm_register_persist_hooks_fn() {
    g_hw_rhythm_snapshot = rhythm_snapshot_impl;
    g_hw_rhythm_apply = rhythm_apply_impl;
    g_hw_rhythm_overlay = rhythm_overlay_impl;
    g_hw_rhythm_mod = rhythm_mod_mode;
}
