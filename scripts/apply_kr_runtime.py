"""Prefix per-module runtime symbols with KR-> in engine sources (one-shot refactor helper)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "src"
FILES = [
    ROOT / "krono_hw_engine.cpp",
    ROOT / "krono_gamma_impl.inc",
    ROOT / "krono_rhythm_impl.inc",
]

SYMS = """
g_io g_tap_ivals g_tap_idx g_ext_last_rise_ms g_ext_last_isr_ms g_ext_validated_iv g_ext_ivals g_ext_idx g_ext_active
g_def_next_mult g_def_div_ctr g_def_wait_f1 g_euc_step_a g_euc_step_b g_mu_last_a g_mu_last_b g_mu_st_a g_mu_st_b
g_sw_on g_sw_off g_poly_next g_poly_off g_lg_pa g_lg_pb g_ph_a g_ph_b g_ph_delta g_lx g_ly g_lz g_px g_py g_pz
g_ch_trig g_ch_xc g_ch_yc g_bin_step g_bin_bank g_bin_pend g_bin_pbank g_bin_next
g22_step g22_frozen g23_pat g23_step g24_fire g25_abs g25_next g25_active g26_mult g26_a_open g26_f1cnt g26_last_swap g27_inv
g_gcf_var g_gcf_ratchet_double g_gcf_anti_half g_gcf_muted g_gcf_next_mult g_gcf_div_ctr g_gcf_wait_f1 g_gcf_base_T
r_drift_pat r_drift_active r_drift_prob r_drift_ramp_up r_drift_step r_drift_next r_drift_calc
r_fill_density r_fill_ramp_up r_fill_mask r_fill_step r_fill_next r_fill_calc
r_skip_prob r_skip_active r_skip_ramp_up r_skip_step r_skip_next r_skip_calc
r_st_active r_st_len r_st_ramp_up r_st_var r_st_seq r_st_step r_st_next r_st_calc
r_mp_a r_mp_b r_mp_m r_mp_frozen r_mp_step r_mp_next r_mp_gen r_mp_calc
r_mu_muted r_mu_var r_mu_count r_mu_ramp_up r_mu_step r_mu_next r_mu_calc
r_dn_pat r_dn_pct r_dn_step r_dn_next r_dn_pending r_dn_calc
r_sg_gen r_sg_var r_sg_song_step r_sg_cur_step r_sg_next r_sg_var_pend r_sg_seed r_sg_nonce r_sg_calc
r_ac_count r_ac_accum_step r_ac_add_pend r_ac_step r_ac_next r_ac_calc r_ac_flags r_ac_phase r_ac_var r_ac_bars
""".split()


def main() -> None:
    syms = sorted(set(s for s in SYMS if s), key=len, reverse=True)
    for path in FILES:
        text = path.read_text(encoding="utf-8")
        for s in syms:
            text = re.sub(rf"(?<!->)\b{s}\b", f"KR->{s}", text)
        path.write_text(text, encoding="utf-8")
    print("OK:", len(FILES), "files", len(syms), "symbols")


if __name__ == "__main__":
    main()
