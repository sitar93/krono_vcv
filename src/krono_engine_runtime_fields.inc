// Included inside `struct HwEngine::EngineRuntime { ... }` (krono_hw_engine.cpp).
// Per-module rhythm / gamma state (was file-scope static — shared across Rack instances).

    IoHub g_io;

    uint32_t g_tap_ivals[NUM_INTERVALS_FOR_AVG]{};
    uint8_t g_tap_idx = 0;

    uint32_t g_ext_last_rise_ms = 0;
    uint32_t g_ext_last_isr_ms = 0;
    uint32_t g_ext_validated_iv = 0;
    uint32_t g_ext_ivals[NUM_EXT_INTERVALS_FOR_VALIDATION]{};
    uint8_t g_ext_idx = 0;
    bool g_ext_active = false;

    uint32_t g_def_next_mult[K_NUM_FW_JACKS]{};
    uint32_t g_def_div_ctr[K_NUM_FW_JACKS]{};
    bool g_def_wait_f1 = false;

    uint32_t g_euc_step_a[5]{};
    uint32_t g_euc_step_b[5]{};

    uint32_t g_mu_last_a[5]{};
    uint32_t g_mu_last_b[5]{};
    bool g_mu_st_a[5]{};
    bool g_mu_st_b[5]{};

    uint32_t g_sw_on[K_NUM_FW_JACKS]{};
    uint32_t g_sw_off[K_NUM_FW_JACKS]{};

    uint32_t g_poly_next[K_NUM_FW_JACKS]{};
    uint32_t g_poly_off[K_NUM_FW_JACKS]{};

    bool g_lg_pa[6]{};
    bool g_lg_pb[6]{};

    PhasingSt g_ph_a[5]{};
    PhasingSt g_ph_b[5]{};
    uint8_t g_ph_delta = 0;

    float g_lx = 0.1f;
    float g_ly = 0.f;
    float g_lz = 0.f;
    float g_px = 0.1f;
    float g_py = 0.f;
    float g_pz = 0.f;
    uint32_t g_ch_trig[K_NUM_FW_JACKS]{};
    uint32_t g_ch_xc[5]{};
    uint32_t g_ch_yc[5]{};

    uint8_t g_bin_step = 0;
    uint8_t g_bin_bank = 0;
    bool g_bin_pend = false;
    uint8_t g_bin_pbank = 0;
    uint32_t g_bin_next = 0;

    uint8_t g22_step = 0;
    bool g22_frozen = false;

    uint8_t g23_pat = 0;
    uint8_t g23_step = 0;

    int8_t g24_fire = -1;

    uint32_t g25_abs[12][6]{};
    uint8_t g25_next[12]{};
    bool g25_active = false;

    bool g26_mult = false;
    bool g26_a_open[6]{};
    uint8_t g26_f1cnt[6]{};
    uint32_t g26_last_swap[6]{};

    bool g27_inv = false;

    GcfVar g_gcf_var = GcfVar::Ratchet;
    bool g_gcf_ratchet_double = false;
    bool g_gcf_anti_half = false;
    bool g_gcf_muted = false;
    uint32_t g_gcf_next_mult[12]{};
    uint32_t g_gcf_div_ctr[12]{};
    bool g_gcf_wait_f1 = true;
    uint32_t g_gcf_base_T = 60000u / 70u;

    uint16_t r_drift_pat[10]{};
    bool r_drift_active = false;
    uint8_t r_drift_prob = 0;
    bool r_drift_ramp_up = true;
    uint8_t r_drift_step = 0;
    uint32_t r_drift_next = 0;
    CalcMode r_drift_calc = CalcMode::Normal;

    uint8_t r_fill_density = 0;
    bool r_fill_ramp_up = true;
    uint16_t r_fill_mask[10]{};
    uint8_t r_fill_step = 0;
    uint32_t r_fill_next = 0;
    CalcMode r_fill_calc = CalcMode::Normal;

    uint8_t r_skip_prob = 0;
    bool r_skip_active = false;
    bool r_skip_ramp_up = true;
    uint8_t r_skip_step = 0;
    uint32_t r_skip_next = 0;
    CalcMode r_skip_calc = CalcMode::Normal;

    bool r_st_active = false;
    uint8_t r_st_len = 2;
    bool r_st_ramp_up = true;
    uint16_t r_st_var[10]{};
    uint8_t r_st_seq = 0;
    uint8_t r_st_step = 0;
    uint32_t r_st_next = 0;
    CalcMode r_st_calc = CalcMode::Normal;

    uint16_t r_mp_a[10]{};
    uint16_t r_mp_b[10]{};
    uint16_t r_mp_m[10]{};
    bool r_mp_frozen = false;
    uint8_t r_mp_step = 0;
    uint32_t r_mp_next = 0;
    uint32_t r_mp_gen = 0;
    CalcMode r_mp_calc = CalcMode::Normal;

    bool r_mu_muted[10]{};
    uint16_t r_mu_var[10]{};
    uint8_t r_mu_count = 0;
    bool r_mu_ramp_up = true;
    uint8_t r_mu_step = 0;
    uint32_t r_mu_next = 0;
    CalcMode r_mu_calc = CalcMode::Normal;

    uint16_t r_dn_pat[10]{};
    uint8_t r_dn_pct = 100;
    uint8_t r_dn_step = 0;
    uint32_t r_dn_next = 0;
    bool r_dn_pending = true;
    CalcMode r_dn_calc = CalcMode::Normal;

    uint16_t r_sg_gen[10]{};
    uint16_t r_sg_var[10]{};
    uint8_t r_sg_song_step = 0;
    uint8_t r_sg_cur_step = 0;
    uint32_t r_sg_next = 0;
    bool r_sg_var_pend = false;
    uint32_t r_sg_seed = 0xC0FFEE01u;
    uint32_t r_sg_nonce = 0;
    CalcMode r_sg_calc = CalcMode::Normal;

    uint8_t r_ac_count = 1;
    uint32_t r_ac_accum_step = 0;
    bool r_ac_add_pend = false;
    uint8_t r_ac_step = 0;
    uint32_t r_ac_next = 0;
    CalcMode r_ac_calc = CalcMode::Normal;
    bool r_ac_flags[10]{};
    uint8_t r_ac_phase[10]{};
    uint16_t r_ac_var[10]{};
    uint8_t r_ac_bars = 0;
