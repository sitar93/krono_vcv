#pragma once

#include <cstdint>

namespace krono {

// Mirrors firmware operational_mode_t order (20 modes).
enum class OpMode : int {
    Default = 0,
    Euclidean,
    Musical,
    Probabilistic,
    Sequential,
    Swing,
    Polyrhythm,
    Logic,
    Phasing,
    Chaos,
    Fixed, // MODE_FIXED (was "Binary" in older plugin)
    Drift,
    Fill,
    Skip,
    Stutter,
    Morph,
    Mute,
    Density,
    Song,
    Accumulate,
    Count
};

enum class CalcMode : int { Normal = 0, Swapped };

/** Modes 12–20 (indices 11–19): short MOD runs mode gesture, not calc swap / fixed bank. */
inline bool mode_uses_mod_gestures(OpMode m) {
    const int i = (int)m;
    return i >= 11 && i <= 19;
}

struct ModeContext {
    uint32_t current_time_ms = 0;
    uint32_t current_tempo_interval_ms = 0;
    CalcMode calc_mode = CalcMode::Normal;
    bool calc_mode_changed = false;
    bool f1_rising_edge = false;
    uint32_t f1_counter = 0;
    uint32_t ms_since_last_call = 0;
    bool sync_request = false;
    bool bypass_first_update = false;
};

static constexpr int kRhythmNumOutputs = 10;

/// Mirrors firmware krono_state_t rhythm-related fields (for patch JSON).
struct RhythmPersistState {
    bool drift_active = false;
    uint8_t drift_probability = 0;
    bool drift_ramp_up = true;
    uint8_t fill_density = 0;
    bool fill_ramp_up = true;
    bool skip_active = false;
    uint8_t skip_probability = 0;
    bool skip_ramp_up = true;
    bool stutter_active = false;
    uint8_t stutter_length = 2;
    bool stutter_ramp_up = true;
    uint16_t stutter_variation_mask[kRhythmNumOutputs] = {};
    bool morph_frozen = false;
    uint32_t morph_generation = 0;
    uint16_t morph_patterns[kRhythmNumOutputs] = {};
    uint16_t mute_mask = 0;
    uint8_t mute_count = 0;
    bool mute_ramp_up = true;
    uint16_t mute_variation_mask[kRhythmNumOutputs] = {};
    uint8_t density_pct = 100;
    bool density_ramp_up = true;
    uint32_t song_variation_seed = 0xC0FFEE01u;
    bool song_variation_pending = false;
    uint8_t accumulate_active_count = 1;
    bool accumulate_add_pending = false;
    uint16_t accumulate_active_mask = 1;
    uint8_t accumulate_phase_offsets[kRhythmNumOutputs] = {};
    uint16_t accumulate_variation_masks[kRhythmNumOutputs] = {};
};

struct HwEngine {
    static constexpr int NUM_OUTS = 12;
    static constexpr int NUM_FW_JACKS = 18;

    OpMode op_mode = OpMode::Default;
    CalcMode calc_mode = CalcMode::Normal;

    CalcMode calc_mode_per_op[(int)OpMode::Count] = {};

    uint32_t swing_profile_a = 3;
    uint32_t swing_profile_b = 3;
    uint32_t chaos_divisor = 1000;
    uint8_t binary_bank = 0;

    RhythmPersistState rhythm_persist = {};

    double wall_ms = 0.0;
    uint32_t last_engine_ms = 0;
    bool bypass_first_update = false;

    uint32_t active_tempo_interval_ms = 60000 / 70;
    uint32_t last_f1_pulse_time_ms = 0;
    uint32_t last_update_time_ms = 0;
    uint32_t f1_tick_counter = 0;
    bool sync_requested = false;
    bool calc_mode_just_changed = false;

    float out_v[NUM_OUTS] = {};

    HwEngine();

    void on_reset();
    void set_op_mode(OpMode m);
    void toggle_calc_mode();
    /** MODE_FIXED: advance bank on short MOD or swap CV (matches firmware fixed_bank_change). */
    void advance_fixed_bank();
    /** Modes 12–20: short MOD gesture (matches mode_dispatch_mod_press). */
    void rhythm_mod_press(uint32_t now_ms);

    void rhythm_snapshot_persist();
    void rhythm_apply_persist();

    void set_tempo_ms(uint32_t interval_ms, bool is_external, uint32_t event_ms);
    /** Re-align current mode transport/state to an event time (used for tap commit downbeat sync). */
    void resync_to_event(uint32_t event_ms);
    void set_bpm(float bpm);

    void on_external_validated(uint32_t interval_ms, uint32_t event_ms);
    void on_external_timeout();

    void process(float sample_time_sec);

    uint32_t current_time_ms() const { return (uint32_t)wall_ms; }
};

} // namespace krono
