/*
 * bovnar_currency_integration.c
 *
 * This file documents the three minimal surgical patches required in the
 * existing Bovnar C source tree.  It is NOT compiled directly — paste each
 * hunk into its target file as shown.
 *
 * ============================================================================
 * PATCH 1 — bovnar_si_units.c: bvn_parse_base_unit()
 * ============================================================================
 *
 * Find the existing function that parses a byte-string unit token and returns
 * a bvn_unit_base_t enum value (bu_none on failure).  At the END of the
 * existing lookup block, before the failure return, add:
 *
 * --- a/src/bovnar_si_units.c
 * +++ b/src/bovnar_si_units.c
 *
 * +#include "bovnar_currency.h"
 *
 *  static bvn_unit_base_t bvn_parse_base_unit(const uint8_t *s, uint32_t len)
 *  {
 *      ... (existing physical-unit lookup: s, wk, yr, tonne, etc.) ...
 *
 * +    /* Currency / crypto fallback: all-uppercase 3-4 char codes.           */
 * +    int cv = bvn_parse_currency_str(s, len);
 * +    if (cv) return (bvn_unit_base_t)cv;
 *
 *      return bu_none;
 *  }
 *
 * ============================================================================
 * PATCH 2 — bovnar_si_units.c: bvn_base_unit_symbol()
 * ============================================================================
 *
 * Find the function / switch that converts a bvn_unit_base_t to its canonical
 * string symbol (used by bvn_unit_to_string).  Before the default / fallback
 * branch, add:
 *
 * +    if (bvn_unit_is_currency((int)base)) {
 * +        const bvn_currency_info_t *ci = bvn_currency_info((int)base);
 * +        return ci ? ci->code : "?";
 * +    }
 *
 * ============================================================================
 * PATCH 3 — bovnar_units.c: bvn_prefix_unit_valid()
 * ============================================================================
 *
 * Find bvn_prefix_unit_valid(bvn_unit_base_t base, value_unit_prefix_t pfx).
 * Add an early-return guard before the existing rules:
 *
 * +    /* IEC binary prefixes are invalid on any currency or crypto unit.      */
 * +    if (!bvn_currency_prefix_valid((int)base, (int)pfx.system))
 * +        return false;
 *
 * ============================================================================
 * PATCH 4 — bovnar.h: BVN_VALUE_BASE_UNIT_COUNT
 * ============================================================================
 *
 * Replace:
 * -  #define BVN_VALUE_BASE_UNIT_COUNT  134
 * With:
 * +  #define BVN_VALUE_BASE_UNIT_COUNT  329
 *
 * And append to the value_base_unit_t enum (after bu_bushel = 133):
 *
 * +  /* ── ISO 4217 fiat currencies ──────────────────────── */
 * +  bu_aed = 134, bu_afn, bu_all, bu_amd, bu_ang, bu_aoa, bu_ars, bu_aud,
 * +  bu_awg, bu_azn, bu_bam, bu_bbd, bu_bdt, bu_bgn, bu_bhd, bu_bmd,
 * +  bu_bnd, bu_bob, bu_brl, bu_bsd, bu_btn, bu_bwp, bu_byn, bu_bzd,
 * +  bu_cad, bu_cdf, bu_chf, bu_clf, bu_clp, bu_cny, bu_cop, bu_crc,
 * +  bu_cup_, bu_cve, bu_czk, bu_djf, bu_dkk, bu_dop, bu_dzd, bu_egp,
 *
 * NOTE: `bu_cup_` uses a trailing underscore because `bu_cup` is already
 * defined as the physical cup volume unit (value 81).  The wire code is
 * still the string "CUP".  Same convention as `bu_try_` / TRY.
 * +  bu_ern, bu_etb, bu_eur, bu_fjd, bu_fkp, bu_gbp, bu_gel, bu_ghs,
 * +  bu_gip, bu_gmd, bu_gnf, bu_gtq, bu_gyd, bu_hkd, bu_hnl, bu_hrk,
 * +  bu_htg, bu_huf, bu_idr, bu_ils, bu_inr, bu_iqd, bu_irr, bu_isk,
 * +  bu_jmd, bu_jod, bu_jpy, bu_kes, bu_kgs, bu_khr, bu_kmf, bu_kpw,
 * +  bu_krw, bu_kwd, bu_kyd, bu_kzt, bu_lak, bu_lbp, bu_lkr, bu_lrd,
 * +  bu_lsl, bu_lyd, bu_mad, bu_mdl, bu_mga, bu_mkd, bu_mmk, bu_mnt,
 * +  bu_mop, bu_mru, bu_mur, bu_mvr, bu_mwk, bu_mxn, bu_myr, bu_mzn,
 * +  bu_nad, bu_ngn, bu_nio, bu_nok, bu_npr, bu_nzd, bu_omr, bu_pab,
 * +  bu_pen, bu_pgk, bu_php, bu_pkr, bu_pln, bu_pyg, bu_qar, bu_ron,
 * +  bu_rsd, bu_rub, bu_rwf, bu_sar, bu_sbd, bu_scr, bu_sdg, bu_sek,
 * +  bu_sgd, bu_shp, bu_sll, bu_sos, bu_srd, bu_stn, bu_svc, bu_syp,
 * +  bu_szl, bu_thb, bu_tjs, bu_tmt, bu_tnd, bu_top, bu_try_, bu_ttd,
 * +  bu_twd, bu_tzs, bu_uah, bu_ugx, bu_usd, bu_uyu, bu_uzs, bu_ves,
 * +  bu_vnd, bu_vuv, bu_wst, bu_xaf, bu_xag, bu_xau, bu_xcd, bu_xdr,
 * +  bu_xof, bu_xpd, bu_xpf, bu_xpt, bu_xts, bu_yer, bu_zar, bu_zmw,
 * +  bu_zwl,
 * +  /* ── Cryptocurrencies ──────────────────────────────── */
 * +  bu_btc = 295, bu_eth, bu_sol, bu_xrp, bu_bnb, bu_ada, bu_ltc,
 * +  bu_dot, bu_xmr, bu_etc, bu_bch, bu_xlm, bu_fil, bu_icp, bu_trx,
 * +  bu_eos, bu_vet, bu_neo, bu_zec, bu_uni, bu_arb, bu_sui, bu_ton,
 * +  bu_inj, bu_sei, bu_apt, bu_tao, bu_wif,
 * +  bu_doge = 323, bu_link, bu_usdt, bu_usdc, bu_avax, bu_atom,
 *
 * NOTE: `bu_try_` uses a trailing underscore to avoid the C keyword `try`
 * (reserved in C++ and some C compilers with extensions).  The string
 * representation is still "TRY".
 *
 * ============================================================================
 * BUILD SYSTEM
 * ============================================================================
 *
 * Add bovnar_currency.c to CMakeLists.txt alongside the existing sources:
 *
 *   target_sources(bvnr_shared PRIVATE src/bovnar_currency.c)
 *   target_sources(bvnr_static PRIVATE src/bovnar_currency.c)
 *
 * ============================================================================
 * CONFORMANCE TEST ADDITIONS
 * ============================================================================
 *
 * The following test case groups should be added to the conformance suite:
 *
 *   currency_fiat_parse          — <float_dec:64,USD>, <float_dec:64,EUR>, …
 *   currency_crypto_parse        — <float_dec:64,BTC>, <float_dec:64,DOGE>, …
 *   currency_si_prefix           — <float_dec:64,k~USD> valid
 *   currency_iec_prefix_reject   — <float_dec:64,Ki~USD> → error_unit_illegal
 *   currency_compound            — <float_dec:64,USD/oz_t>, <float_dec:64,EUR/h>
 *   currency_exchange_rate       — <float_dec:64,USD/EUR>
 *   currency_array               — <float_dec:64,USD> [1.0, 2.0, 3.0]
 *   currency_inline_unit         — 19.99 USD  (inline suffix)
 *   currency_roundtrip           — write then read, unit preserved
 */
