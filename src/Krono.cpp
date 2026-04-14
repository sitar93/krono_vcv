#include "plugin.hpp"
#include "krono_hw_engine.hpp"

#include <app/Switch.hpp>

#include <algorithm>
#include <array>
#include <ctime>

template<typename T>
static T kClamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

namespace {

static constexpr uint32_t kFwMinInterval = 33;
static constexpr uint32_t kFwMaxInterval = 6000;
static constexpr uint32_t kFwMaxDiff = 3000;
static constexpr uint32_t kTapTimeoutMs = 10000;
static constexpr uint32_t kExtTimeoutMs = 5000;
static constexpr uint32_t kDefaultTempoIv = 60000 / 70;

static constexpr uint32_t kOpModeTapHoldMs = 1000;
static constexpr uint32_t kOpModeBetaHoldMs = 2000;
static constexpr uint32_t kOpModeGammaHoldMs = 3000;
static constexpr uint32_t kOpModeBetaMaxHoldMs = 5000;
static constexpr uint32_t kOpModeConfirmTimeoutMs = 10000;
static constexpr uint32_t kOpModeSaveTimeoutMs = 5000;
static constexpr uint32_t kPa1DebounceMs = 50;
static constexpr uint32_t kCalcSwapMaxMs = 500;
static constexpr uint32_t kCalcSwapCooldownMs = 100;

static constexpr uint32_t kAuxPulseMs = 140;

// Firmware src/variables.h + main.c on_gamma_arm: krono_aux_led_pattern_start(2, ON, GAP)
static constexpr uint32_t AUX_LED_MULTI_PULSE_ON_MS = 55;
static constexpr uint32_t AUX_LED_MULTI_PULSE_GAP_MS = 100;

// Firmware variables.h (status LED)
static constexpr uint32_t STATUS_LED_BASE_INTERVAL_MS = 120;
static constexpr uint32_t STATUS_LED_LONG_ON_MS = 1200;
static constexpr uint32_t STATUS_LED_INTER_PULSE_OFF_MS = 500;
static constexpr uint32_t STATUS_LED_AFTER_LONG_OFF_MS = 650;
static constexpr uint32_t STATUS_LED_SEQUENCE_GAP_MS = 400;

static constexpr int kNumOpModes = (int)krono::OpMode::Count;

static uint8_t status_user_mode_number(int modeIdx) {
    if (modeIdx < 0 || modeIdx >= kNumOpModes)
        return 1;
    return (uint8_t)(modeIdx + 1);
}

/** Matches firmware `status_led.c`: modes 1–9 → u short; ≥10 → tens long + units short (e.g. 21 = L+L+N). */
static uint8_t status_pulse_count(int modeIdx) {
    uint8_t u = status_user_mode_number(modeIdx);
    if (u < 10u)
        return u;
    return (uint8_t)(u / 10u + u % 10u);
}

static bool status_pulse_is_long(int modeIdx, uint8_t pulse_index) {
    uint8_t u = status_user_mode_number(modeIdx);
    if (u < 10u)
        return false;
    uint8_t tens = (uint8_t)(u / 10u);
    return pulse_index < tens;
}

/// Mirrors firmware `status_led.c` (logical on = full brightness).
struct HwStatusLed {
    uint32_t last_blink_time = 0;
    bool led_state = false;
    int current_mode_for_led = 0;
    uint8_t blink_count = 0;
    uint32_t active_on_duration_ms = STATUS_LED_BASE_INTERVAL_MS;
    uint32_t pending_off_gap_ms = STATUS_LED_INTER_PULSE_OFF_MS;

    bool override_active = false;
    bool override_fixed_state = false;
    float out_brightness = 0.f;

    void set_led(bool on) {
        led_state = on;
        out_brightness = on ? 1.f : 0.f;
    }

    void reset_sequence(uint32_t now) {
        if (!override_active) {
            blink_count = 0;
            pending_off_gap_ms = STATUS_LED_INTER_PULSE_OFF_MS ? STATUS_LED_INTER_PULSE_OFF_MS : 1u;
            last_blink_time = now;
            set_led(false);
        }
    }

    void sync_mode(int modeIdx, uint32_t now) {
        if (modeIdx < 0 || modeIdx >= kNumOpModes)
            modeIdx = 0;
        if (modeIdx != current_mode_for_led && !override_active) {
            current_mode_for_led = modeIdx;
            reset_sequence(now);
        } else {
            current_mode_for_led = modeIdx;
        }
    }

    void set_override(bool active, bool fixed_state, uint32_t now) {
        const bool was = override_active;
        override_active = active;
        override_fixed_state = fixed_state;
        if (active && !was)
            set_led(fixed_state);
        else if (!active && was)
            reset_sequence(now);
        else if (active && was)
            set_led(fixed_state);
    }

    void update(uint32_t current_time_ms) {
        if (override_active) {
            set_led(override_fixed_state);
            return;
        }

        const uint8_t total_pulses = status_pulse_count(current_mode_for_led);
        const uint32_t sequence_pause = STATUS_LED_SEQUENCE_GAP_MS;

        if (blink_count >= total_pulses) {
            if (current_time_ms - last_blink_time >= sequence_pause) {
                blink_count = 0;
                pending_off_gap_ms = STATUS_LED_INTER_PULSE_OFF_MS ? STATUS_LED_INTER_PULSE_OFF_MS : 1u;
                last_blink_time = current_time_ms;
                set_led(false);
            } else {
                if (led_state)
                    set_led(false);
                return;
            }
        }

        if (!led_state) {
            const uint32_t gap = pending_off_gap_ms ? pending_off_gap_ms : 1u;
            if (current_time_ms - last_blink_time >= gap) {
                active_on_duration_ms = status_pulse_is_long(current_mode_for_led, blink_count)
                                              ? STATUS_LED_LONG_ON_MS
                                              : STATUS_LED_BASE_INTERVAL_MS;
                if (active_on_duration_ms == 0)
                    active_on_duration_ms = 1;
                set_led(true);
                last_blink_time = current_time_ms;
            }
        } else {
            if (current_time_ms - last_blink_time >= active_on_duration_ms) {
                const uint8_t finished_pulse_idx = blink_count;
                set_led(false);
                last_blink_time = current_time_ms;
                blink_count++;
                pending_off_gap_ms = status_pulse_is_long(current_mode_for_led, finished_pulse_idx)
                                         ? (STATUS_LED_AFTER_LONG_OFF_MS ? STATUS_LED_AFTER_LONG_OFF_MS : 1u)
                                         : (STATUS_LED_INTER_PULSE_OFF_MS ? STATUS_LED_INTER_PULSE_OFF_MS : 1u);
            }
        }
    }
};

enum class OpSmState {
    Idle,
    TapHeldQualifying,
    TapQualifiedWaitingRelease,
    AwaitingModPressOrTimeout,
    AwaitingConfirmTap
};

enum class CalcSwapSmState { Idle, ModePressed };

static json_t* u16_array_json(const uint16_t* p, int n) {
    json_t* a = json_array();
    for (int i = 0; i < n; i++)
        json_array_append_new(a, json_integer(p[i]));
    return a;
}

static void u16_array_from_json(json_t* a, uint16_t* p, int n) {
    if (!a || !json_is_array(a))
        return;
    size_t idx;
    json_t* el;
    json_array_foreach(a, idx, el) {
        if ((int)idx < n)
            p[idx] = (uint16_t)json_integer_value(el);
    }
}

static json_t* rhythm_persist_to_json(const krono::RhythmPersistState& r) {
    json_t* o = json_object();
    json_object_set_new(o, "drift_active", r.drift_active ? json_true() : json_false());
    json_object_set_new(o, "drift_probability", json_integer(r.drift_probability));
    json_object_set_new(o, "drift_ramp_up", r.drift_ramp_up ? json_true() : json_false());
    json_object_set_new(o, "fill_density", json_integer(r.fill_density));
    json_object_set_new(o, "fill_ramp_up", r.fill_ramp_up ? json_true() : json_false());
    json_object_set_new(o, "skip_active", r.skip_active ? json_true() : json_false());
    json_object_set_new(o, "skip_probability", json_integer(r.skip_probability));
    json_object_set_new(o, "skip_ramp_up", r.skip_ramp_up ? json_true() : json_false());
    json_object_set_new(o, "stutter_active", r.stutter_active ? json_true() : json_false());
    json_object_set_new(o, "stutter_length", json_integer(r.stutter_length));
    json_object_set_new(o, "stutter_ramp_up", r.stutter_ramp_up ? json_true() : json_false());
    json_object_set_new(o, "stutter_variation_mask", u16_array_json(r.stutter_variation_mask, 10));
    json_object_set_new(o, "morph_frozen", r.morph_frozen ? json_true() : json_false());
    json_object_set_new(o, "morph_generation", json_integer((json_int_t)r.morph_generation));
    json_object_set_new(o, "morph_patterns", u16_array_json(r.morph_patterns, 10));
    json_object_set_new(o, "mute_mask", json_integer(r.mute_mask));
    json_object_set_new(o, "mute_count", json_integer(r.mute_count));
    json_object_set_new(o, "mute_ramp_up", r.mute_ramp_up ? json_true() : json_false());
    json_object_set_new(o, "mute_variation_mask", u16_array_json(r.mute_variation_mask, 10));
    json_object_set_new(o, "density_pct", json_integer(r.density_pct));
    json_object_set_new(o, "density_ramp_up", r.density_ramp_up ? json_true() : json_false());
    json_object_set_new(o, "song_variation_seed", json_integer((json_int_t)r.song_variation_seed));
    json_object_set_new(o, "song_variation_pending", r.song_variation_pending ? json_true() : json_false());
    json_object_set_new(o, "accumulate_active_count", json_integer(r.accumulate_active_count));
    json_object_set_new(o, "accumulate_add_pending", r.accumulate_add_pending ? json_true() : json_false());
    json_object_set_new(o, "accumulate_active_mask", json_integer(r.accumulate_active_mask));
    json_t* ph = json_array();
    for (int i = 0; i < 10; i++)
        json_array_append_new(ph, json_integer(r.accumulate_phase_offsets[i]));
    json_object_set_new(o, "accumulate_phase_offsets", ph);
    json_object_set_new(o, "accumulate_variation_masks", u16_array_json(r.accumulate_variation_masks, 10));
    json_object_set_new(o, "gamma_seq_freeze_frozen", r.gamma_seq_freeze_frozen ? json_true() : json_false());
    json_object_set_new(o, "gamma_seq_freeze_step", json_integer(r.gamma_seq_freeze_step));
    json_object_set_new(o, "gamma_seq_trip_pattern", json_integer(r.gamma_seq_trip_pattern));
    json_object_set_new(o, "gamma_seq_trip_step", json_integer(r.gamma_seq_trip_step));
    json_object_set_new(o, "gamma_portals_div_on_a", r.gamma_portals_div_on_a ? json_true() : json_false());
    json_object_set_new(o, "gamma_coin_invert", r.gamma_coin_invert ? json_true() : json_false());
    json_object_set_new(o, "gamma_ratchet_double", r.gamma_ratchet_double ? json_true() : json_false());
    json_object_set_new(o, "gamma_antiratchet_half", r.gamma_antiratchet_half ? json_true() : json_false());
    json_object_set_new(o, "gamma_startstop_muted", r.gamma_startstop_muted ? json_true() : json_false());
    return o;
}

static void rhythm_persist_from_json(json_t* o, krono::RhythmPersistState& r) {
    if (!o || !json_is_object(o))
        return;
    json_t* j;
    if ((j = json_object_get(o, "drift_active")))
        r.drift_active = json_is_true(j);
    if ((j = json_object_get(o, "drift_probability")))
        r.drift_probability = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "drift_ramp_up")))
        r.drift_ramp_up = json_is_true(j);
    if ((j = json_object_get(o, "fill_density")))
        r.fill_density = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "fill_ramp_up")))
        r.fill_ramp_up = json_is_true(j);
    if ((j = json_object_get(o, "skip_active")))
        r.skip_active = json_is_true(j);
    if ((j = json_object_get(o, "skip_probability")))
        r.skip_probability = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "skip_ramp_up")))
        r.skip_ramp_up = json_is_true(j);
    if ((j = json_object_get(o, "stutter_active")))
        r.stutter_active = json_is_true(j);
    if ((j = json_object_get(o, "stutter_length")))
        r.stutter_length = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "stutter_ramp_up")))
        r.stutter_ramp_up = json_is_true(j);
    if ((j = json_object_get(o, "stutter_variation_mask")))
        u16_array_from_json(j, r.stutter_variation_mask, 10);
    if ((j = json_object_get(o, "morph_frozen")))
        r.morph_frozen = json_is_true(j);
    if ((j = json_object_get(o, "morph_generation")))
        r.morph_generation = (uint32_t)json_integer_value(j);
    if ((j = json_object_get(o, "morph_patterns")))
        u16_array_from_json(j, r.morph_patterns, 10);
    if ((j = json_object_get(o, "mute_mask")))
        r.mute_mask = (uint16_t)json_integer_value(j);
    if ((j = json_object_get(o, "mute_count")))
        r.mute_count = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "mute_ramp_up")))
        r.mute_ramp_up = json_is_true(j);
    if ((j = json_object_get(o, "mute_variation_mask")))
        u16_array_from_json(j, r.mute_variation_mask, 10);
    if ((j = json_object_get(o, "density_pct")))
        r.density_pct = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "density_ramp_up")))
        r.density_ramp_up = json_is_true(j);
    if ((j = json_object_get(o, "song_variation_seed")))
        r.song_variation_seed = (uint32_t)json_integer_value(j);
    if ((j = json_object_get(o, "song_variation_pending")))
        r.song_variation_pending = json_is_true(j);
    if ((j = json_object_get(o, "accumulate_active_count")))
        r.accumulate_active_count = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "accumulate_add_pending")))
        r.accumulate_add_pending = json_is_true(j);
    if ((j = json_object_get(o, "accumulate_active_mask")))
        r.accumulate_active_mask = (uint16_t)json_integer_value(j);
    if ((j = json_object_get(o, "accumulate_phase_offsets")) && json_is_array(j)) {
        size_t idx;
        json_t* el;
        json_array_foreach(j, idx, el) {
            if ((int)idx < 10)
                r.accumulate_phase_offsets[idx] = (uint8_t)json_integer_value(el);
        }
    }
    if ((j = json_object_get(o, "accumulate_variation_masks")))
        u16_array_from_json(j, r.accumulate_variation_masks, 10);
    if ((j = json_object_get(o, "gamma_seq_freeze_frozen")))
        r.gamma_seq_freeze_frozen = json_is_true(j);
    if ((j = json_object_get(o, "gamma_seq_freeze_step")))
        r.gamma_seq_freeze_step = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "gamma_seq_trip_pattern")))
        r.gamma_seq_trip_pattern = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "gamma_seq_trip_step")))
        r.gamma_seq_trip_step = (uint8_t)json_integer_value(j);
    if ((j = json_object_get(o, "gamma_portals_div_on_a")))
        r.gamma_portals_div_on_a = json_is_true(j);
    if ((j = json_object_get(o, "gamma_coin_invert")))
        r.gamma_coin_invert = json_is_true(j);
    if ((j = json_object_get(o, "gamma_ratchet_double")))
        r.gamma_ratchet_double = json_is_true(j);
    if ((j = json_object_get(o, "gamma_antiratchet_half")))
        r.gamma_antiratchet_half = json_is_true(j);
    if ((j = json_object_get(o, "gamma_startstop_muted")))
        r.gamma_startstop_muted = json_is_true(j);
}

} // namespace

/** ~10% smaller than SmallSimpleLight (2 mm → 1.8 mm). Spento: centro trasparente (si vede il pannello);
 *  acceso: nucleo chiaro + alone più marcato. */
template <typename TBaseColor>
struct KronoLed : SmallSimpleLight<TBaseColor> {
    KronoLed() {
        this->box.size = mm2px(math::Vec(1.8f, 1.8f));
        this->bgColor = nvgRGBA(0, 0, 0, 0);
        this->borderColor = nvgRGBA(0xff, 0xff, 0xff, 0.28f);
    }

    void drawLight(const widget::Widget::DrawArgs& args) override {
        if (this->color.a <= 0.0)
            return;
        const float cx = this->box.size.x * 0.5f;
        const float cy = this->box.size.y * 0.5f;
        const float radius = std::min(this->box.size.x, this->box.size.y) * 0.5f;
        const NVGcolor c = this->color;
        const float a = c.a;

        const NVGcolor hot = nvgRGBAf(1.f, 1.f, 1.f, std::min(1.f, a * 0.95f));
        const NVGcolor edge = nvgRGBAf(
            std::min(1.f, c.r * 1.65f + 0.08f),
            std::min(1.f, c.g * 1.65f + 0.08f),
            std::min(1.f, c.b * 1.65f + 0.08f),
            std::min(1.f, a));

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, radius);
        const NVGpaint paint =
            nvgRadialGradient(args.vg, cx, cy, radius * 0.08f, radius * 0.94f, hot, edge);
        nvgFillPaint(args.vg, paint);
        nvgFill(args.vg);
    }

    void drawHalo(const widget::Widget::DrawArgs& args) override {
        if (args.fb)
            return;
        const float halo = settings::haloBrightness;
        if (halo == 0.f)
            return;
        if (this->color.r == 0.f && this->color.g == 0.f && this->color.b == 0.f)
            return;

        const math::Vec c = this->box.size.div(2);
        const float radius = std::min(this->box.size.x, this->box.size.y) / 2.f;
        const float oradius = radius + std::min(radius * 4.f, 15.f);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, c.x - oradius, c.y - oradius, 2.f * oradius, 2.f * oradius);

        NVGcolor icol = color::mult(this->color, halo * 1.65f);
        const NVGcolor ocol = nvgRGBA(0, 0, 0, 0);
        const NVGpaint paint = nvgRadialGradient(args.vg, c.x, c.y, radius, oradius, icol, ocol);
        nvgFillPaint(args.vg, paint);
        nvgFill(args.vg);
    }
};

/** TAP / MOD: momentary, disco circolare (anello scuro + centro bianco come il pannello fisico).
 *  Lato ~7 mm: con centri a xA/xB (~9,6 mm) due quadrati 11,5 mm si sovrapponevano. */
struct KronoPanelButton : app::Switch {
    KronoPanelButton() {
        momentary = true;
        box.size = mm2px(math::Vec(7.0f, 7.0f));
    }

    void draw(const DrawArgs& args) override {
        const math::Vec s = box.size;
        const float cx = s.x * 0.5f;
        const float cy = s.y * 0.5f;
        const float R = std::max(0.5f, std::min(s.x, s.y) * 0.5f - 0.6f);
        bool pressed = false;
        if (engine::ParamQuantity* pq = getParamQuantity())
            pressed = pq->getValue() > pq->getMinValue() + 0.001f;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, R);
        nvgFillColor(args.vg, pressed ? nvgRGB(0x22, 0x22, 0x22) : nvgRGB(0x14, 0x14, 0x14));
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, R);
        nvgStrokeWidth(args.vg, 0.65f);
        nvgStrokeColor(args.vg, nvgRGB(0x42, 0x42, 0x42));
        nvgStroke(args.vg);

        const float rWhite = R * 0.44f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, rWhite);
        nvgFillColor(args.vg, pressed ? nvgRGB(0xd0, 0xd0, 0xd0) : nvgRGB(0xef, 0xef, 0xef));
        nvgFill(args.vg);

        Widget::draw(args);
    }
};

struct Krono : Module {
    enum ParamIds {
        TAP_PARAM,
        MOD_PARAM,
        NUM_PARAMS
    };

    enum InputIds {
        CLOCK_INPUT,
        SWAP_INPUT,
        NUM_INPUTS
    };

    enum OutputIds {
        OUT_1A_OUTPUT,
        OUT_2A_OUTPUT,
        OUT_3A_OUTPUT,
        OUT_4A_OUTPUT,
        OUT_5A_OUTPUT,
        OUT_6A_OUTPUT,
        OUT_1B_OUTPUT,
        OUT_2B_OUTPUT,
        OUT_3B_OUTPUT,
        OUT_4B_OUTPUT,
        OUT_5B_OUTPUT,
        OUT_6B_OUTPUT,
        NUM_OUTPUTS
    };

    enum LightIds {
        STATUS_LED_LIGHT,
        AUX_LED_LIGHT,
        OUT_1A_LIGHT,
        OUT_2A_LIGHT,
        OUT_3A_LIGHT,
        OUT_4A_LIGHT,
        OUT_5A_LIGHT,
        OUT_6A_LIGHT,
        OUT_1B_LIGHT,
        OUT_2B_LIGHT,
        OUT_3B_LIGHT,
        OUT_4B_LIGHT,
        OUT_5B_LIGHT,
        OUT_6B_LIGHT,
        NUM_LIGHTS
    };

    krono::HwEngine eng;

    dsp::SchmittTrigger tapTrig;
    dsp::SchmittTrigger swapCvTrig;
    dsp::SchmittTrigger clockTrig;
    float prev_clock_cv = 0.f;
    float prev_swap_cv = 0.f;
    uint32_t last_clock_edge_ms = 0;
    uint32_t last_swap_edge_ms = 0;
    bool clock_uni_armed = true;
    bool swap_uni_armed = true;
    bool clock_bi_armed = true;
    bool swap_bi_armed = true;

    uint32_t lastTapMs = UINT32_MAX;
    std::array<uint32_t, 3> tapIntervalsMs = {};
    uint8_t tapIntervalCount = 0;
    uint32_t lastTapEventMs = 0;

    uint32_t extLastRiseMs = 0;
    uint32_t extLastIsrMs = 0;
    std::array<uint32_t, 3> extIntervalsMs = {};
    uint8_t extIntervalIdx = 0;

    bool external_clock_active = false;
    uint32_t last_valid_external_clock_interval = 0;
    uint32_t last_reported_tap_interval = 0;

    int last_known_main_op_mode = 0;

    OpSmState op_sm = OpSmState::Idle;
    uint32_t tap_press_start_time = 0;
    uint8_t op_mode_clicks_count = 0;
    bool just_exited_op_mode_sm = false;
    uint32_t tap_release_time_for_timeout_logic = 0;
    bool mod_pressed_during_tap_hold_phase = false;
    uint32_t mode_confirm_state_enter_time = 0;
    bool tap_confirm_action_taken_this_press = false;

    uint32_t pa1_mod_change_last_event_time = 0;
    bool pa1_mod_change_last_debounced_state = false;
    bool pa1_mod_change_current_raw_state = false;
    bool pa1_mod_change_last_raw_state = false;

    bool op_mode_select_beta = false;
    bool op_mode_beta_threshold_announced = false;
    bool op_mode_select_gamma = false;
    bool op_mode_gamma_threshold_announced = false;

    CalcSwapSmState calc_swap_sm = CalcSwapSmState::Idle;
    uint32_t calc_swap_mode_press_start_time = 0;
    uint32_t last_calc_swap_trigger_time = 0;

    HwStatusLed status_led;
    uint32_t aux_led_until_ms = 0;

    /** Mirrors krono_aux_led_pattern.c (PAT_ON / PAT_GAP) for Gamma arm on PA3. */
    enum class AuxGammaPatPhase : uint8_t { On, Gap };
    bool aux_gamma_double_active = false;
    uint8_t aux_gamma_pulses_done = 0;
    AuxGammaPatPhase aux_gamma_phase = AuxGammaPatPhase::On;
    uint32_t aux_gamma_deadline = 0;

    /** Single normal-length flash on status + aux (`kAuxPulseMs`, same as `pulse_aux`). */
    uint32_t dual_confirm_until_ms = 0;

    void dual_confirm_pattern_abort() { dual_confirm_until_ms = 0; }

    void aux_gamma_pattern_abort() {
        aux_gamma_double_active = false;
        aux_gamma_pulses_done = 0;
        aux_gamma_deadline = 0;
    }

    void pulse_aux(uint32_t now) {
        dual_confirm_pattern_abort();
        aux_gamma_pattern_abort();
        aux_led_until_ms = now + kAuxPulseMs;
    }

    /** krono_aux_led_pattern_start(2, AUX_LED_MULTI_PULSE_ON_MS, AUX_LED_MULTI_PULSE_GAP_MS) */
    void pulse_aux_gamma(uint32_t now) {
        dual_confirm_pattern_abort();
        aux_led_until_ms = 0;
        aux_gamma_double_active = true;
        aux_gamma_pulses_done = 0;
        aux_gamma_phase = AuxGammaPatPhase::On;
        aux_gamma_deadline = now + (AUX_LED_MULTI_PULSE_ON_MS ? AUX_LED_MULTI_PULSE_ON_MS : 1u);
    }

    void pulse_status_and_aux_confirm(uint32_t now) {
        aux_gamma_pattern_abort();
        aux_led_until_ms = 0;
        dual_confirm_until_ms = now + kAuxPulseMs;
    }

    void tick_aux_gamma_pattern(uint32_t now) {
        while (aux_gamma_double_active) {
            if ((int32_t)(now - aux_gamma_deadline) < 0)
                break;
            if (aux_gamma_phase == AuxGammaPatPhase::On) {
                aux_gamma_pulses_done++;
                if (aux_gamma_pulses_done >= 2) {
                    aux_gamma_pattern_abort();
                    return;
                }
                aux_gamma_phase = AuxGammaPatPhase::Gap;
                aux_gamma_deadline = now + (AUX_LED_MULTI_PULSE_GAP_MS ? AUX_LED_MULTI_PULSE_GAP_MS : 1u);
            } else {
                aux_gamma_phase = AuxGammaPatPhase::On;
                aux_gamma_deadline = now + (AUX_LED_MULTI_PULSE_ON_MS ? AUX_LED_MULTI_PULSE_ON_MS : 1u);
            }
        }
    }
    static bool hysteresis_rise(float v, float low, float high, bool& armed) {
        if (v <= low)
            armed = true;
        if (armed && v >= high) {
            armed = false;
            return true;
        }
        return false;
    }
    static bool cv_edge_detect(float v, float& prev_v, dsp::SchmittTrigger& trig, uint32_t now_ms, uint32_t& last_edge_ms,
                               bool& uni_armed, bool& bi_armed) {
        const bool schmitt_edge = trig.process(v);
        const bool bipolar_edge = hysteresis_rise(v, -0.05f, 0.05f, bi_armed);
        const bool unipolar_edge = hysteresis_rise(v, 0.2f, 0.5f, uni_armed);
        const bool edge = schmitt_edge || bipolar_edge || unipolar_edge;
        prev_v = v;
        if (!edge)
            return false;
        // Prevent double-triggers from mixed detectors on the same waveform cycle.
        static constexpr uint32_t kMinEdgeGapMs = 20;
        if (last_edge_ms != 0 && (now_ms - last_edge_ms) < kMinEdgeGapMs)
            return false;
        last_edge_ms = now_ms;
        return true;
    }

    void reset_op_mode_sm_vars(uint32_t now) {
        op_sm = OpSmState::Idle;
        tap_press_start_time = 0;
        op_mode_clicks_count = 0;
        status_led.set_override(false, false, now);
        tap_release_time_for_timeout_logic = 0;
        mod_pressed_during_tap_hold_phase = false;
        mode_confirm_state_enter_time = 0;

        pa1_mod_change_last_event_time = 0;
        pa1_mod_change_last_debounced_state = false;
        pa1_mod_change_current_raw_state = false;
        pa1_mod_change_last_raw_state = false;

        op_mode_select_beta = false;
        op_mode_beta_threshold_announced = false;
        op_mode_select_gamma = false;
        op_mode_gamma_threshold_announced = false;

        just_exited_op_mode_sm = true;
    }

    void reset_calc_swap_sm_vars() {
        calc_swap_sm = CalcSwapSmState::Idle;
        calc_swap_mode_press_start_time = 0;
    }

    void handle_button_calc_mode_swap(uint32_t now, bool mod_is_pressed_raw) {
        switch (calc_swap_sm) {
            case CalcSwapSmState::Idle:
                if (mod_is_pressed_raw) {
                    if (op_sm == OpSmState::Idle) {
                        calc_swap_sm = CalcSwapSmState::ModePressed;
                        calc_swap_mode_press_start_time = now;
                    }
                }
                break;
            case CalcSwapSmState::ModePressed:
                if (!mod_is_pressed_raw) {
                    if (now - calc_swap_mode_press_start_time <= kCalcSwapMaxMs) {
                        if (now - last_calc_swap_trigger_time > kCalcSwapCooldownMs) {
                            if (krono::mode_uses_mod_gestures(eng.op_mode)) {
                                eng.rhythm_mod_press(now);
                                pulse_aux(now);
                            } else if (eng.op_mode == krono::OpMode::Fixed) {
                                eng.advance_fixed_bank();
                                pulse_aux(now);
                            } else {
                                eng.toggle_calc_mode();
                                pulse_aux(now);
                            }
                            last_calc_swap_trigger_time = now;
                        }
                    }
                    reset_calc_swap_sm_vars();
                } else {
                    if (now - calc_swap_mode_press_start_time > kCalcSwapMaxMs)
                        reset_calc_swap_sm_vars();
                }
                break;
        }
    }

    void handle_op_mode_sm(uint32_t now, bool tap_pressed_now, bool mod_is_pressed_raw) {
        pa1_mod_change_current_raw_state = mod_is_pressed_raw;
        if (pa1_mod_change_current_raw_state != pa1_mod_change_last_raw_state)
            pa1_mod_change_last_event_time = now;
        pa1_mod_change_last_raw_state = pa1_mod_change_current_raw_state;

        const bool old_debounced_mod_state = pa1_mod_change_last_debounced_state;
        if ((now - pa1_mod_change_last_event_time) > kPa1DebounceMs) {
            if (pa1_mod_change_current_raw_state != pa1_mod_change_last_debounced_state)
                pa1_mod_change_last_debounced_state = pa1_mod_change_current_raw_state;
        }
        const bool mod_button_is_debounced_pressed = pa1_mod_change_last_debounced_state;
        const bool mod_button_just_pressed_debounced =
            (mod_button_is_debounced_pressed && !old_debounced_mod_state);
        const bool mod_button_just_released_debounced =
            (!mod_button_is_debounced_pressed && old_debounced_mod_state);

        switch (op_sm) {
            case OpSmState::Idle:
                if (just_exited_op_mode_sm && !tap_pressed_now)
                    just_exited_op_mode_sm = false;
                if (!just_exited_op_mode_sm && tap_pressed_now && calc_swap_sm == CalcSwapSmState::Idle) {
                    op_sm = OpSmState::TapHeldQualifying;
                    tap_press_start_time = now;
                    pa1_mod_change_last_debounced_state = mod_is_pressed_raw;
                    pa1_mod_change_last_event_time = now;
                }
                break;

            case OpSmState::TapHeldQualifying:
                if (!tap_pressed_now) {
                    reset_op_mode_sm_vars(now);
                } else if (now - tap_press_start_time >= kOpModeTapHoldMs) {
                    mod_pressed_during_tap_hold_phase = false;
                    op_mode_clicks_count = 0;
                    op_mode_select_beta = false;
                    op_mode_beta_threshold_announced = false;
                    op_mode_select_gamma = false;
                    op_mode_gamma_threshold_announced = false;
                    status_led.set_override(true, false, now);
                    pulse_aux(now);
                    op_sm = OpSmState::TapQualifiedWaitingRelease;
                }
                break;

            case OpSmState::TapQualifiedWaitingRelease:
                if (tap_pressed_now) {
                    if ((now - tap_press_start_time) >= kOpModeBetaMaxHoldMs) {
                        reset_op_mode_sm_vars(now);
                        break;
                    }
                    if (!op_mode_gamma_threshold_announced &&
                        (now - tap_press_start_time) >= kOpModeGammaHoldMs) {
                        op_mode_gamma_threshold_announced = true;
                        op_mode_select_gamma = true;
                        op_mode_select_beta = false;
                        pulse_aux_gamma(now);
                    } else if (!op_mode_beta_threshold_announced &&
                               (now - tap_press_start_time) >= kOpModeBetaHoldMs) {
                        op_mode_beta_threshold_announced = true;
                        op_mode_select_beta = true;
                        pulse_aux(now);
                    }
                    if (mod_button_is_debounced_pressed)
                        status_led.set_override(true, true, now);
                    else
                        status_led.set_override(true, false, now);
                    if (mod_button_just_released_debounced) {
                        op_mode_clicks_count++;
                        mod_pressed_during_tap_hold_phase = true;
                    }
                } else {
                    status_led.set_override(true, false, now);
                    if (mod_pressed_during_tap_hold_phase) {
                        if (op_mode_clicks_count > 0) {
                            op_sm = OpSmState::AwaitingConfirmTap;
                            mode_confirm_state_enter_time = now;
                            tap_confirm_action_taken_this_press = false;
                        } else {
                            reset_op_mode_sm_vars(now);
                        }
                    } else {
                        tap_release_time_for_timeout_logic = now;
                        op_sm = OpSmState::AwaitingModPressOrTimeout;
                    }
                }
                break;

            case OpSmState::AwaitingModPressOrTimeout:
                if (mod_button_just_pressed_debounced)
                    tap_release_time_for_timeout_logic = 0;
                if (mod_button_is_debounced_pressed)
                    status_led.set_override(true, true, now);
                else
                    status_led.set_override(true, false, now);
                if (mod_button_just_released_debounced) {
                    op_mode_clicks_count = 1;
                    op_sm = OpSmState::AwaitingConfirmTap;
                    mode_confirm_state_enter_time = now;
                    tap_confirm_action_taken_this_press = false;
                    return;
                }
                if (tap_release_time_for_timeout_logic != 0 &&
                    (now - tap_release_time_for_timeout_logic >= kOpModeSaveTimeoutMs)) {
                    pulse_status_and_aux_confirm(now);
                    reset_op_mode_sm_vars(now);
                }
                break;

            case OpSmState::AwaitingConfirmTap:
                if (mod_button_is_debounced_pressed)
                    status_led.set_override(true, true, now);
                else
                    status_led.set_override(true, false, now);
                if (mod_button_just_released_debounced) {
                    op_mode_clicks_count++;
                    mode_confirm_state_enter_time = now;
                }

                if (tap_pressed_now) {
                    if (!tap_confirm_action_taken_this_press) {
                        if (op_mode_clicks_count > 0) {
                            pulse_status_and_aux_confirm(now);
                            const uint8_t clicks = op_mode_clicks_count;
                            if (op_mode_select_gamma) {
                                uint8_t gn = clicks;
                                if (gn > 10)
                                    gn = (uint8_t)(((gn - 1u) % 10u) + 1u);
                                const int idx = (int)gn + 19;
                                if (idx >= 0 && idx < kNumOpModes)
                                    eng.set_op_mode((krono::OpMode)idx);
                            } else if (op_mode_select_beta) {
                                uint8_t n = clicks;
                                if (n > 10)
                                    n = (uint8_t)(((n - 1u) % 10u) + 1u);
                                const int idx = (int)n + 9;
                                if (idx >= 0 && idx < kNumOpModes)
                                    eng.set_op_mode((krono::OpMode)idx);
                            } else {
                                if (clicks > 0 && clicks <= (uint8_t)kNumOpModes)
                                    eng.set_op_mode((krono::OpMode)(clicks - 1));
                            }
                        }
                        reset_op_mode_sm_vars(now);
                        tap_confirm_action_taken_this_press = true;
                    }
                } else {
                    tap_confirm_action_taken_this_press = false;
                }

                if (now - mode_confirm_state_enter_time >= kOpModeConfirmTimeoutMs)
                    reset_op_mode_sm_vars(now);
                break;

            default:
                reset_op_mode_sm_vars(now);
                break;
        }
    }

    Krono() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configButton(TAP_PARAM, "Tap");
        configButton(MOD_PARAM, "MOD");

        configInput(CLOCK_INPUT, "External clock");
        configInput(SWAP_INPUT, "Swap / bank CV");

        const char* labels[NUM_OUTPUTS] = {
            "1A", "2A", "3A", "4A", "5A", "6A",
            "1B", "2B", "3B", "4B", "5B", "6B"};
        for (int i = 0; i < NUM_OUTPUTS; i++)
            configOutput(i, labels[i]);

        configLight(STATUS_LED_LIGHT, "Status");
        configLight(AUX_LED_LIGHT, "Aux");
        const char* ledLabels[NUM_OUTPUTS] = {
            "1A LED", "2A LED", "3A LED", "4A LED", "5A LED", "6A LED",
            "1B LED", "2B LED", "3B LED", "4B LED", "5B LED", "6B LED"};
        for (int i = 0; i < NUM_OUTPUTS; i++)
            configLight(OUT_1A_LIGHT + i, ledLabels[i]);
    }

    void onAdd(const AddEvent& e) override { (void)e; }

    json_t* dataToJson() override {
        eng.rhythm_snapshot_persist();
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "tempoIntervalMs", json_integer(eng.active_tempo_interval_ms));
        json_object_set_new(rootJ, "opMode", json_integer((int)eng.op_mode));
        json_object_set_new(rootJ, "swingA", json_integer(eng.swing_profile_a));
        json_object_set_new(rootJ, "swingB", json_integer(eng.swing_profile_b));
        json_object_set_new(rootJ, "chaosDivisor", json_integer(eng.chaos_divisor));
        json_object_set_new(rootJ, "binaryBank", json_integer(eng.binary_bank));
        json_t* arr = json_array();
        for (int i = 0; i < kNumOpModes; i++)
            json_array_append_new(arr, json_integer((int)eng.calc_mode_per_op[i]));
        json_object_set_new(rootJ, "calcPerOp", arr);
        json_object_set_new(rootJ, "rhythm", rhythm_persist_to_json(eng.rhythm_persist));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* j;
        if ((j = json_object_get(rootJ, "swingA")))
            eng.swing_profile_a = (uint32_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "swingB")))
            eng.swing_profile_b = (uint32_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "chaosDivisor")))
            eng.chaos_divisor = (uint32_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "binaryBank")))
            eng.binary_bank = (uint8_t)json_integer_value(j);
        if ((j = json_object_get(rootJ, "calcPerOp")) && json_is_array(j)) {
            size_t idx;
            json_t* el;
            json_array_foreach(j, idx, el) {
                if ((int)idx < kNumOpModes)
                    eng.calc_mode_per_op[idx] = json_integer_value(el) ? krono::CalcMode::Swapped : krono::CalcMode::Normal;
            }
        }
        if ((j = json_object_get(rootJ, "rhythm")))
            rhythm_persist_from_json(j, eng.rhythm_persist);
        if ((j = json_object_get(rootJ, "opMode"))) {
            int m = (int)json_integer_value(j);
            eng.set_op_mode((krono::OpMode)kClamp(m, 0, kNumOpModes - 1));
        }
        if ((j = json_object_get(rootJ, "tempoIntervalMs")))
            eng.active_tempo_interval_ms = (uint32_t)json_integer_value(j);
        eng.calc_mode = eng.calc_mode_per_op[(int)eng.op_mode];
        last_reported_tap_interval = eng.active_tempo_interval_ms;
        last_known_main_op_mode = (int)eng.op_mode;
    }

    void onReset() override {
        Module::onReset();
        std::srand((unsigned)std::time(nullptr));
        eng.on_reset();
        tapIntervalCount = 0;
        extIntervalIdx = 0;
        lastTapMs = UINT32_MAX;
        extLastRiseMs = extLastIsrMs = 0;
        external_clock_active = false;
        last_valid_external_clock_interval = 0;
        last_reported_tap_interval = kDefaultTempoIv;
        last_known_main_op_mode = 0;
        reset_calc_swap_sm_vars();
        reset_op_mode_sm_vars(0);
        just_exited_op_mode_sm = false;
        aux_led_until_ms = 0;
        aux_gamma_pattern_abort();
        dual_confirm_pattern_abort();
    }

    void process(const ProcessArgs& args) override {
        const uint32_t nowMs = eng.current_time_ms();

        const bool tap_held = params[TAP_PARAM].getValue() > 0.5f;
        const bool mod_raw = params[MOD_PARAM].getValue() > 0.5f;
        const bool tap_rise = tapTrig.process(params[TAP_PARAM].getValue());
        const bool op_sm_was_idle = (op_sm == OpSmState::Idle);

        handle_op_mode_sm(nowMs, tap_held, mod_raw);
        last_known_main_op_mode = (int)eng.op_mode;

        // Track tap-tempo on rising edge even if op-mode SM transitions
        // to TapHeldQualifying in the same frame.
        if (!external_clock_active && op_sm_was_idle && tap_rise) {
            if (lastTapMs != UINT32_MAX) {
                uint32_t iv = nowMs - lastTapMs;
                if (iv >= kFwMinInterval && iv <= kFwMaxInterval) {
                    tapIntervalsMs[tapIntervalCount++] = iv;
                    if (tapIntervalCount >= 3) {
                        uint64_t sum = 0;
                        uint32_t mn = UINT32_MAX, mx = 0;
                        for (int i = 0; i < 3; i++) {
                            sum += tapIntervalsMs[i];
                            mn = std::min(mn, tapIntervalsMs[i]);
                            mx = std::max(mx, tapIntervalsMs[i]);
                        }
                        if (mx - mn <= kFwMaxDiff) {
                            uint32_t avg = (uint32_t)(sum / 3);
                            avg = kClamp(avg, kFwMinInterval, kFwMaxInterval);
                            eng.set_tempo_ms(avg, false, nowMs);
                            eng.resync_to_event(nowMs);
                            last_reported_tap_interval = avg;
                            pulse_aux(nowMs);
                        }
                        tapIntervalCount = 0;
                    }
                } else {
                    tapIntervalCount = 0;
                }
            }
            lastTapMs = nowMs;
            lastTapEventMs = nowMs;
        }
        if (!external_clock_active && nowMs > lastTapEventMs + kTapTimeoutMs && lastTapEventMs != 0)
            tapIntervalCount = 0;

        if (op_sm != OpSmState::Idle) {
            if (calc_swap_sm != CalcSwapSmState::Idle)
                reset_calc_swap_sm_vars();
        } else {
            if (inputs[CLOCK_INPUT].isConnected()) {
                const float cv = inputs[CLOCK_INPUT].getVoltage();
                const bool clock_edge =
                    cv_edge_detect(cv, prev_clock_cv, clockTrig, nowMs, last_clock_edge_ms, clock_uni_armed, clock_bi_armed);
                if (clock_edge) {
                    if (extLastRiseMs != 0) {
                        uint32_t iv = nowMs - extLastRiseMs;
                        if (iv >= kFwMinInterval && iv <= kFwMaxInterval) {
                            extIntervalsMs[extIntervalIdx++] = iv;
                            if (extIntervalIdx >= 3) {
                                uint64_t sum = 0;
                                uint32_t mn = UINT32_MAX, mx = 0;
                                for (int i = 0; i < 3; i++) {
                                    sum += extIntervalsMs[i];
                                    mn = std::min(mn, extIntervalsMs[i]);
                                    mx = std::max(mx, extIntervalsMs[i]);
                                }
                                if (mx - mn <= kFwMaxDiff) {
                                    uint32_t avg = (uint32_t)(sum / 3);
                                    avg = kClamp(avg, kFwMinInterval, kFwMaxInterval);
                                    if (!external_clock_active || avg != last_valid_external_clock_interval) {
                                        eng.on_external_validated(avg, nowMs);
                                    }
                                    external_clock_active = true;
                                    last_valid_external_clock_interval = avg;
                                    tapIntervalCount = 0;
                                    lastTapMs = UINT32_MAX;
                                }
                                extIntervalIdx = 0;
                            }
                        } else {
                            extIntervalIdx = 0;
                        }
                    }
                    extLastRiseMs = nowMs;
                    extLastIsrMs = nowMs;
                }
                if (extLastIsrMs != 0 && nowMs > extLastIsrMs + kExtTimeoutMs) {
                    if (external_clock_active) {
                        external_clock_active = false;
                        uint32_t new_iv = kDefaultTempoIv;
                        if (last_valid_external_clock_interval > 0 &&
                            last_valid_external_clock_interval >= kFwMinInterval &&
                            last_valid_external_clock_interval <= kFwMaxInterval) {
                            new_iv = last_valid_external_clock_interval;
                        } else if (last_reported_tap_interval > 0 &&
                                   last_reported_tap_interval >= kFwMinInterval &&
                                   last_reported_tap_interval <= kFwMaxInterval) {
                            new_iv = last_reported_tap_interval;
                        }
                        last_reported_tap_interval = new_iv;
                        eng.on_external_timeout();
                        eng.set_tempo_ms(new_iv, false, nowMs);
                        last_valid_external_clock_interval = 0;
                    }
                }
            } else {
                extLastRiseMs = extLastIsrMs = 0;
                extIntervalIdx = 0;
                prev_clock_cv = 0.f;
                last_clock_edge_ms = 0;
                clock_uni_armed = true;
                clock_bi_armed = true;
                if (external_clock_active) {
                    external_clock_active = false;
                    eng.on_external_timeout();
                    uint32_t new_iv = kDefaultTempoIv;
                    if (last_valid_external_clock_interval > 0 &&
                        last_valid_external_clock_interval >= kFwMinInterval &&
                        last_valid_external_clock_interval <= kFwMaxInterval) {
                        new_iv = last_valid_external_clock_interval;
                    } else if (last_reported_tap_interval > 0 &&
                               last_reported_tap_interval >= kFwMinInterval &&
                               last_reported_tap_interval <= kFwMaxInterval) {
                        new_iv = last_reported_tap_interval;
                    }
                    last_reported_tap_interval = new_iv;
                    eng.set_tempo_ms(new_iv, false, nowMs);
                    last_valid_external_clock_interval = 0;
                }
            }

            if (!external_clock_active) {
                handle_button_calc_mode_swap(nowMs, mod_raw);

                if (inputs[SWAP_INPUT].isConnected() &&
                    ([&]() {
                        const float sv = inputs[SWAP_INPUT].getVoltage();
                        return cv_edge_detect(
                            sv, prev_swap_cv, swapCvTrig, nowMs, last_swap_edge_ms, swap_uni_armed, swap_bi_armed);
                    })()) {
                    if (nowMs - last_calc_swap_trigger_time > kCalcSwapCooldownMs) {
                        if (krono::mode_uses_mod_gestures(eng.op_mode)) {
                            eng.rhythm_mod_press(nowMs);
                            pulse_aux(nowMs);
                        } else if (eng.op_mode == krono::OpMode::Fixed) {
                            eng.advance_fixed_bank();
                            pulse_aux(nowMs);
                        } else {
                            eng.toggle_calc_mode();
                            pulse_aux(nowMs);
                        }
                        last_calc_swap_trigger_time = nowMs;
                    }
                }
            } else {
                prev_swap_cv = 0.f;
                last_swap_edge_ms = 0;
                swap_uni_armed = true;
                swap_bi_armed = true;
            }
        }

        eng.process(args.sampleTime);

        const uint32_t nowLed = eng.current_time_ms();
        status_led.sync_mode((int)eng.op_mode, nowLed);
        status_led.update(nowLed);
        tick_aux_gamma_pattern(nowLed);
        const bool dualConfirmOn = dual_confirm_until_ms != 0 && nowLed < dual_confirm_until_ms;
        const float statusBright = dualConfirmOn ? 1.f : status_led.out_brightness;
        const float auxBright = dualConfirmOn
                                    ? 1.f
                                    : (aux_gamma_double_active
                                           ? (aux_gamma_phase == AuxGammaPatPhase::On ? 1.f : 0.f)
                                           : (nowLed < aux_led_until_ms ? 1.f : 0.f));
        lights[STATUS_LED_LIGHT].setBrightness(statusBright);
        lights[AUX_LED_LIGHT].setBrightness(auxBright);

        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].setVoltage(eng.out_v[i]);
            lights[OUT_1A_LIGHT + i].setBrightnessSmooth(eng.out_v[i] > 1.f ? 1.f : 0.f, args.sampleTime);
        }
    }
};

/** Center of LED on the module widget (pixels, origin top-left), from KronoPanel.jpg coords (top-left, 156×1024).
 *  Same letterbox as KronoPanelBackground::draw — avoids img→mm→mm2px drift vs the painted bitmap. */
static math::Vec kronoLedCenterPxFromPanelImgPx(float ix, float iy, math::Vec boxPx) {
    const float imgW = 156.f;
    const float imgH = 1024.f;
    const float imgAspect = imgW / imgH;
    const float boxAspect = boxPx.x / boxPx.y;
    float drawW = boxPx.x;
    float drawH = boxPx.y;
    float drawX = 0.f;
    float drawY = 0.f;
    if (imgAspect < boxAspect) {
        drawW = boxPx.y * imgAspect;
        drawX = (boxPx.x - drawW) * 0.5f;
    } else if (imgAspect > boxAspect) {
        drawH = boxPx.x / imgAspect;
        drawY = (boxPx.y - drawH) * 0.5f;
    }
    const float cx = drawX + (ix / imgW) * drawW;
    const float cyTop = drawY + (iy / imgH) * drawH;
    return math::Vec(cx, cyTop);
}

template <typename TLight>
static void addLightCenteredAtPanelImgPx(ModuleWidget* mw, Krono* module, int lightId, float ix, float iy) {
    auto* l = new TLight;
    l->module = module;
    l->firstLightId = lightId;
    l->box.pos = kronoLedCenterPxFromPanelImgPx(ix, iy, mw->box.size).minus(l->box.size.div(2));
    mw->addChild(l);
}

struct KronoPanelBackground : Widget {
    std::string panelPath;
    KronoPanelBackground(const std::string& path) : panelPath(path) {}

    void draw(const DrawArgs& args) override {
        auto img = APP->window->loadImage(panelPath);
        if (!img || img->handle < 0)
            return;
        const float imgW = 156.f;
        const float imgH = 1024.f;
        const float imgAspect = imgW / imgH;
        const float boxAspect = box.size.x / box.size.y;
        float drawW = box.size.x;
        float drawH = box.size.y;
        float drawX = 0.f;
        float drawY = 0.f;
        if (imgAspect < boxAspect) {
            drawW = box.size.y * imgAspect;
            drawX = (box.size.x - drawW) * 0.5f;
        } else if (imgAspect > boxAspect) {
            drawH = box.size.x / imgAspect;
            drawY = (box.size.y - drawH) * 0.5f;
        }
        nvgBeginPath(args.vg);
        nvgRect(args.vg, drawX, drawY, drawW, drawH);
        NVGpaint paint = nvgImagePattern(args.vg, drawX, drawY, drawW, drawH, 0.f, img->handle, 1.f);
        nvgFillPaint(args.vg, paint);
        nvgFill(args.vg);
    }
};

struct KronoWidget : ModuleWidget {
    KronoWidget(Krono* module) {
        setModule(module);
        box.size = Vec(RACK_GRID_WIDTH * 4.f, RACK_GRID_HEIGHT);
        auto* bg = new KronoPanelBackground(asset::plugin(pluginInstance, "res/KronoPanel.jpg"));
        bg->box.size = box.size;
        addChild(bg);

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Jack / pulsanti (mm): già allineati al pannello.
        const float xA = 5.40f;
        const float xB = 14.98f;
        const float yButton = 17.41f;
        const float yInput = 29.13f;
        const float yOut[6] = {43.80f, 55.58f, 70.26f, 82.03f, 96.70f, 108.47f};

        // LED: pixel sul JPG 156×1024; posizionamento in pixel modulo (stessa letterbox dello sfondo).
        // Taratura: tools/krono-led-tuner.html
        static constexpr float kLedRedTapIx = 26.52f;
        static constexpr float kLedRedTapIy = 166.88f;
        static constexpr float kLedRedModIx = 131.63f;
        static constexpr float kLedRedModIy = 166.88f;
        static constexpr float kLedBlueAIx[6] = {58.89f, 58.90f, 58.19f, 58.19f, 58.90f, 58.19f};
        static constexpr float kLedBlueBIx[6] = {135.29f, 135.29f, 135.29f, 134.57f, 135.29f, 134.57f};
        static constexpr float kLedBlueIy[6] = {397.60f, 502.74f, 607.85f, 713.68f, 818.79f, 923.11f};

        addParam(createParamCentered<KronoPanelButton>(mm2px(Vec(xA, yButton)), module, Krono::TAP_PARAM));
        addParam(createParamCentered<KronoPanelButton>(mm2px(Vec(xB, yButton)), module, Krono::MOD_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xA, yInput)), module, Krono::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(xB, yInput)), module, Krono::SWAP_INPUT));

        addLightCenteredAtPanelImgPx<KronoLed<RedLight>>(this, module, Krono::STATUS_LED_LIGHT, kLedRedTapIx, kLedRedTapIy);
        addLightCenteredAtPanelImgPx<KronoLed<RedLight>>(this, module, Krono::AUX_LED_LIGHT, kLedRedModIx, kLedRedModIy);

        for (int i = 0; i < 6; i++) {
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xA, yOut[i])), module, Krono::OUT_1A_OUTPUT + i));
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(xB, yOut[i])), module, Krono::OUT_1B_OUTPUT + i));
            addLightCenteredAtPanelImgPx<KronoLed<BlueLight>>(this, module, Krono::OUT_1A_LIGHT + i, kLedBlueAIx[i], kLedBlueIy[i]);
            addLightCenteredAtPanelImgPx<KronoLed<BlueLight>>(this, module, Krono::OUT_1B_LIGHT + i, kLedBlueBIx[i], kLedBlueIy[i]);
        }
    }
};

Model* modelKrono = createModel<Krono, KronoWidget>("Krono");
