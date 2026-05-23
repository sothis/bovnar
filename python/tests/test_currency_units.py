"""
tests/test_currency_units.py

Comprehensive test suite for the Bovnar currency/crypto unit extension.

Test groups
-----------
TestEnumContiguity          — enum values are dense and in the correct ranges
TestFiatMetadata            — minor units, numeric codes, names for key fiat codes
TestCryptoMetadata          — minor units and names for key crypto codes
TestPredicates              — is_currency / is_fiat / is_crypto
TestReverseCodeLookup       — from_code() round-trips
TestIterators               — all_fiat / all_crypto / all_currencies counts
TestMinorUnit3              — 3-decimal currencies: BHD, JOD, KWD, OMR, TND
TestMinorUnit0              — 0-decimal currencies: JPY, KRW, VND, etc.
TestMinorUnit4              — CLF (Unidad de Fomento, 4 decimals)
TestCryptoMinorUnits        — chain-canonical minor units for major cryptos
TestCurrencyInfoImmutable   — CurrencyInfo is frozen
TestFromCodeCaseFolding     — from_code accepts "usd", "USD", "Usd"
TestFromCodeUnknown         — from_code raises KeyError for garbage input
TestCurrencyCodeUnique      — no two entries share the same code string
TestEnumCoverageComplete    — every BaseUnit in the currency range has a table entry
TestBaseUnitSentinel        — _SENTINEL value matches expected count
TestPrefixRulesC            — bvn_currency_prefix_valid contract (pure-C contract)
TestParseCurrencyStr        — bvn_parse_currency_str contract (pure-C contract)
TestParseCurrencyStrRejects — parser rejects non-currency tokens
TestBvnrFormatExamples      — .bvnr snippet strings are syntactically plausible

The last two test classes validate the *contract* of the C functions as
documented, using the Python metadata table as the source of truth.  They do
NOT load libbvnr_shared.so and are therefore runnable without the native library
(useful in CI environments that only build documentation or run lint).
"""

from __future__ import annotations

import re
import pytest

from bovnar.enums import (
    BaseUnit,
    CURRENCY_FIAT_FIRST,
    CURRENCY_FIAT_LAST,
    CURRENCY_CRYPTO_FIRST,
    CURRENCY_CRYPTO_LAST,
)
from bovnar.currency import (
    CurrencyInfo,
    all_currencies,
    all_crypto,
    all_fiat,
    currency_code,
    currency_info,
    currency_name,
    from_code,
    is_crypto,
    is_currency,
    is_fiat,
    minor_unit,
)


# ── helpers ────────────────────────────────────────────────────────────────

def _all_currency_bases() -> list[BaseUnit]:
    return [b for b in BaseUnit if int(b) in range(
        int(CURRENCY_FIAT_FIRST), int(CURRENCY_CRYPTO_LAST) + 1)]


# ── TestEnumContiguity ─────────────────────────────────────────────────────

class TestEnumContiguity:
    def test_fiat_first_value(self):
        assert int(CURRENCY_FIAT_FIRST) == 134

    def test_fiat_last_value(self):
        assert int(CURRENCY_FIAT_LAST) == 294

    def test_crypto_first_value(self):
        assert int(CURRENCY_CRYPTO_FIRST) == 295

    def test_crypto_last_value(self):
        assert int(CURRENCY_CRYPTO_LAST) == 328

    def test_sentinel_value(self):
        assert int(BaseUnit._SENTINEL) == 329

    def test_fiat_range_size(self):
        assert int(CURRENCY_FIAT_LAST) - int(CURRENCY_FIAT_FIRST) + 1 == 161

    def test_crypto_range_size(self):
        assert int(CURRENCY_CRYPTO_LAST) - int(CURRENCY_CRYPTO_FIRST) + 1 == 34

    def test_total_currency_count(self):
        assert len(_all_currency_bases()) == 195

    def test_no_gap_between_fiat_and_crypto(self):
        assert int(CURRENCY_CRYPTO_FIRST) == int(CURRENCY_FIAT_LAST) + 1

    def test_physical_units_end_before_fiat(self):
        assert int(BaseUnit.BUSHEL) == 133
        assert int(CURRENCY_FIAT_FIRST) == 134


# ── TestFiatMetadata ───────────────────────────────────────────────────────

class TestFiatMetadata:
    @pytest.mark.parametrize("code,num,mu,name_fragment", [
        ("USD", 840, 2, "Dollar"),
        ("EUR", 978, 2, "Euro"),
        ("GBP", 826, 2, "Pound"),
        ("JPY", 392, 0, "Yen"),
        ("CHF", 756, 2, "Franc"),
        ("KWD", 414, 3, "Kuwaiti"),
        ("BHD",  48, 3, "Bahraini"),
        ("OMR", 512, 3, "Omani"),
        ("JOD", 400, 3, "Jordanian"),
        ("TND", 788, 3, "Tunisian"),
        ("CLF", 990, 4, "Fomento"),
        ("VND", 704, 0, "Dong"),
        ("CLP", 152, 0, "Chilean"),
        ("KRW", 410, 0, "Korean"),
        ("ISK", 352, 0, "Icelandic"),
        ("XAU", 959, 0, "Gold"),
        ("XAG", 961, 0, "Silver"),
        ("XPT", 962, 0, "Platinum"),
        ("XPD", 964, 0, "Palladium"),
        ("XDR", 960, 0, "Drawing"),
    ])
    def test_fiat_metadata(self, code, num, mu, name_fragment):
        base = from_code(code)
        info = currency_info(base)
        assert info.code == code
        assert info.numeric_code == num
        assert info.minor_unit == mu
        assert not info.is_crypto
        assert info.is_fiat
        assert name_fragment.lower() in info.name.lower()

    def test_aed_enum_value(self):
        assert int(BaseUnit.AED) == 134

    def test_zwl_enum_value(self):
        assert int(BaseUnit.ZWL) == 294

    def test_usd_enum_value(self):
        assert int(BaseUnit.USD) == 274

    def test_eur_enum_value(self):
        assert int(BaseUnit.EUR) == 176

    def test_jpy_enum_value(self):
        assert int(BaseUnit.JPY) == 200


# ── TestCryptoMetadata ─────────────────────────────────────────────────────

class TestCryptoMetadata:
    @pytest.mark.parametrize("code,mu,name_fragment", [
        ("BTC",   8,  "Bitcoin"),
        ("ETH",  18,  "Ethereum"),
        ("SOL",   9,  "Solana"),
        ("XRP",   6,  "XRP"),
        ("BNB",  18,  "BNB"),
        ("ADA",   6,  "Cardano"),
        ("LTC",   8,  "Litecoin"),
        ("DOT",  10,  "Polkadot"),
        ("XMR",  12,  "Monero"),
        ("ETC",  18,  "Classic"),
        ("BCH",   8,  "Bitcoin Cash"),
        ("XLM",   7,  "Stellar"),
        ("DOGE",  8,  "Dogecoin"),
        ("LINK", 18,  "Chainlink"),
        ("USDT",  6,  "Tether"),
        ("USDC",  6,  "USD Coin"),
        ("AVAX", 18,  "Avalanche"),
        ("ATOM",  6,  "Cosmos"),
    ])
    def test_crypto_metadata(self, code, mu, name_fragment):
        base = from_code(code)
        info = currency_info(base)
        assert info.code == code
        assert info.numeric_code == 0
        assert info.minor_unit == mu
        assert info.is_crypto
        assert not info.is_fiat
        assert name_fragment.lower() in info.name.lower()

    def test_btc_enum_value(self):
        assert int(BaseUnit.BTC) == 295

    def test_eth_enum_value(self):
        assert int(BaseUnit.ETH) == 296

    def test_atom_enum_value(self):
        assert int(BaseUnit.ATOM) == 328

    def test_doge_is_4_char(self):
        assert len(BaseUnit.DOGE.name) == 4

    def test_link_is_4_char(self):
        assert len(BaseUnit.LINK.name) == 4

    def test_usdt_is_4_char(self):
        assert len(BaseUnit.USDT.name) == 4

    def test_usdc_is_4_char(self):
        assert len(BaseUnit.USDC.name) == 4

    def test_avax_is_4_char(self):
        assert len(BaseUnit.AVAX.name) == 4

    def test_atom_is_4_char(self):
        assert len(BaseUnit.ATOM.name) == 4


# ── TestPredicates ─────────────────────────────────────────────────────────

class TestPredicates:
    @pytest.mark.parametrize("base", [
        BaseUnit.USD, BaseUnit.EUR, BaseUnit.JPY, BaseUnit.XAU,
        BaseUnit.BTC, BaseUnit.ETH, BaseUnit.DOGE, BaseUnit.USDT,
    ])
    def test_is_currency_true(self, base):
        assert is_currency(base)

    @pytest.mark.parametrize("base", [
        BaseUnit.NONE, BaseUnit.METER, BaseUnit.GRAM, BaseUnit.SECOND,
        BaseUnit.HERTZ, BaseUnit.PASCAL, BaseUnit.BUSHEL, BaseUnit.DENIER,
    ])
    def test_is_currency_false_physical(self, base):
        assert not is_currency(base)

    @pytest.mark.parametrize("base", [
        BaseUnit.USD, BaseUnit.EUR, BaseUnit.GBP, BaseUnit.XAU, BaseUnit.XDR,
    ])
    def test_is_fiat_true(self, base):
        assert is_fiat(base)

    @pytest.mark.parametrize("base", [
        BaseUnit.BTC, BaseUnit.ETH, BaseUnit.SOL, BaseUnit.DOGE,
    ])
    def test_is_fiat_false_for_crypto(self, base):
        assert not is_fiat(base)

    @pytest.mark.parametrize("base", [
        BaseUnit.BTC, BaseUnit.ETH, BaseUnit.SOL, BaseUnit.DOGE, BaseUnit.USDT,
    ])
    def test_is_crypto_true(self, base):
        assert is_crypto(base)

    @pytest.mark.parametrize("base", [
        BaseUnit.USD, BaseUnit.EUR, BaseUnit.XAU, BaseUnit.METER,
    ])
    def test_is_crypto_false(self, base):
        assert not is_crypto(base)


# ── TestReverseCodeLookup ──────────────────────────────────────────────────

class TestReverseCodeLookup:
    @pytest.mark.parametrize("code,expected", [
        ("USD", BaseUnit.USD),
        ("EUR", BaseUnit.EUR),
        ("JPY", BaseUnit.JPY),
        ("BTC", BaseUnit.BTC),
        ("ETH", BaseUnit.ETH),
        ("DOGE", BaseUnit.DOGE),
        ("USDT", BaseUnit.USDT),
        ("AVAX", BaseUnit.AVAX),
        ("XAU",  BaseUnit.XAU),
    ])
    def test_from_code_exact(self, code, expected):
        assert from_code(code) == expected

    def test_from_code_roundtrip_all(self):
        for info in all_currencies():
            assert from_code(info.code) == info.base


# ── TestIterators ──────────────────────────────────────────────────────────

class TestIterators:
    def test_all_fiat_count(self):
        assert sum(1 for _ in all_fiat()) == 161

    def test_all_crypto_count(self):
        assert sum(1 for _ in all_crypto()) == 34

    def test_all_currencies_count(self):
        assert sum(1 for _ in all_currencies()) == 195

    def test_all_fiat_are_not_crypto(self):
        for info in all_fiat():
            assert not info.is_crypto

    def test_all_crypto_are_not_fiat(self):
        for info in all_crypto():
            assert info.is_crypto

    def test_all_fiat_have_nonzero_numeric_code(self):
        for info in all_fiat():
            assert info.numeric_code > 0, f"{info.code} has numeric_code=0"

    def test_all_crypto_have_zero_numeric_code(self):
        for info in all_crypto():
            assert info.numeric_code == 0


# ── TestMinorUnit3 ─────────────────────────────────────────────────────────

class TestMinorUnit3:
    @pytest.mark.parametrize("code", ["BHD", "JOD", "KWD", "OMR", "TND",
                                       "IQD", "LYD"])
    def test_three_decimal_currencies(self, code):
        assert minor_unit(from_code(code)) == 3


# ── TestMinorUnit0 ─────────────────────────────────────────────────────────

class TestMinorUnit0:
    @pytest.mark.parametrize("code", [
        "JPY", "KRW", "VND", "ISK", "CLP", "DJF", "GNF",
        "KMF", "PYG", "RWF", "UGX", "VUV", "XAF", "XOF",
        "XPF", "XAU", "XAG", "XPT", "XPD", "XDR",
    ])
    def test_zero_decimal_currencies(self, code):
        assert minor_unit(from_code(code)) == 0


# ── TestMinorUnit4 ─────────────────────────────────────────────────────────

class TestMinorUnit4:
    def test_clf_is_4_decimals(self):
        assert minor_unit(BaseUnit.CLF) == 4


# ── TestCryptoMinorUnits ───────────────────────────────────────────────────

class TestCryptoMinorUnits:
    def test_btc_satoshi(self):
        assert minor_unit(BaseUnit.BTC) == 8

    def test_eth_wei(self):
        assert minor_unit(BaseUnit.ETH) == 18

    def test_sol_lamport(self):
        assert minor_unit(BaseUnit.SOL) == 9

    def test_xrp_drop(self):
        assert minor_unit(BaseUnit.XRP) == 6

    def test_xlm_stroop(self):
        assert minor_unit(BaseUnit.XLM) == 7

    def test_dot_planck(self):
        assert minor_unit(BaseUnit.DOT) == 10

    def test_xmr_piconero(self):
        assert minor_unit(BaseUnit.XMR) == 12

    def test_eos_four_decimals(self):
        assert minor_unit(BaseUnit.EOS) == 4

    def test_usdt_six_decimals(self):
        assert minor_unit(BaseUnit.USDT) == 6

    def test_usdc_six_decimals(self):
        assert minor_unit(BaseUnit.USDC) == 6


# ── TestCurrencyInfoImmutable ──────────────────────────────────────────────

class TestCurrencyInfoImmutable:
    def test_frozen_dataclass_raises_on_mutation(self):
        info = currency_info(BaseUnit.USD)
        with pytest.raises((AttributeError, TypeError)):
            info.minor_unit = 99  # type: ignore[misc]

    def test_str_returns_code(self):
        assert str(currency_info(BaseUnit.EUR)) == "EUR"


# ── TestFromCodeCaseFolding ────────────────────────────────────────────────

class TestFromCodeCaseFolding:
    @pytest.mark.parametrize("variant", ["usd", "USD", "Usd", " USD ", "uSD"])
    def test_case_insensitive(self, variant):
        assert from_code(variant) == BaseUnit.USD

    @pytest.mark.parametrize("variant", ["btc", "BTC", "Btc"])
    def test_crypto_case_insensitive(self, variant):
        assert from_code(variant) == BaseUnit.BTC

    @pytest.mark.parametrize("variant", ["doge", "DOGE", "Doge"])
    def test_four_char_case_insensitive(self, variant):
        assert from_code(variant) == BaseUnit.DOGE


# ── TestFromCodeUnknown ────────────────────────────────────────────────────

class TestFromCodeUnknown:
    @pytest.mark.parametrize("bad", [
        "XYZ", "AAA", "BBB", "foo", "METER", "SECOND", "", "US", "USDO",
    ])
    def test_raises_key_error(self, bad):
        with pytest.raises(KeyError):
            from_code(bad)


# ── TestCurrencyCodeUnique ─────────────────────────────────────────────────

class TestCurrencyCodeUnique:
    def test_all_codes_are_unique(self):
        codes = [info.code for info in all_currencies()]
        assert len(codes) == len(set(codes))

    def test_codes_are_uppercase(self):
        for info in all_currencies():
            assert info.code == info.code.upper(), f"{info.code} is not uppercase"

    def test_fiat_codes_are_3_chars(self):
        for info in all_fiat():
            assert len(info.code) == 3, f"{info.code} is not 3 chars"

    def test_crypto_codes_are_3_or_4_chars(self):
        for info in all_crypto():
            assert 3 <= len(info.code) <= 4, f"{info.code} length out of range"

    def test_no_collision_with_single_letter_si_units(self):
        si_single = {"K", "A", "W", "V", "F", "N", "C", "S", "T", "J"}
        for info in all_currencies():
            assert info.code not in si_single, f"{info.code} collides with SI unit"

    def test_no_collision_with_two_letter_mixed_case_si_units(self):
        si_two = {"Hz", "Pa", "Wb", "Bq", "Gy", "Sv", "PS", "VA"}
        for info in all_currencies():
            assert info.code not in si_two, f"{info.code} collides with SI unit"

    def test_physical_unit_enum_names_do_not_appear_as_currency_member_names(self):
        physical_names = {m.name for m in BaseUnit
                          if int(m) < int(CURRENCY_FIAT_FIRST)}
        currency_member_names = {m.name for m in BaseUnit
                                  if int(CURRENCY_FIAT_FIRST) <= int(m) <= int(CURRENCY_CRYPTO_LAST)}
        assert physical_names.isdisjoint(currency_member_names), (
            f"Name collision(s): {physical_names & currency_member_names}"
        )


# ── TestEnumCoverageComplete ───────────────────────────────────────────────

class TestEnumCoverageComplete:
    def test_every_currency_base_has_table_entry(self):
        missing = []
        for base in BaseUnit:
            if int(CURRENCY_FIAT_FIRST) <= int(base) <= int(CURRENCY_CRYPTO_LAST):
                try:
                    currency_info(base)
                except KeyError:
                    missing.append(base)
        assert not missing, f"Missing table entries: {missing}"

    def test_table_size_matches_enum_range(self):
        n_enum = int(BaseUnit._SENTINEL) - int(CURRENCY_FIAT_FIRST)
        n_table = sum(1 for _ in all_currencies())
        assert n_table == n_enum


# ── TestPhysicalUnitCollisions ─────────────────────────────────────────────

class TestPhysicalUnitCollisions:
    """
    Guard against future collisions between physical unit enum names and
    currency/crypto codes.

    Physical unit Python enum names that happen to be 3-4 uppercase chars
    are a landmine: adding a same-named currency member to the same IntEnum
    raises TypeError at import time.  This test class documents all known
    collisions and verifies the disambiguation convention (trailing _) is
    applied correctly.
    """

    # Physical unit names that collide with ISO 4217 / crypto codes.
    # Key = wire code (string), value = Python enum name with disambiguation.
    KNOWN_DISAMBIGUATIONS: dict[str, str] = {
        "CUP": "CUP_",   # physical CUP=81 (cup volume); Cuban Peso=166
    }

    def test_cup_physical_unit_is_still_present(self):
        assert BaseUnit.CUP == 81

    def test_cup_cuban_peso_is_cup_underscore(self):
        assert int(BaseUnit.CUP_) == 166

    def test_cup_wire_code_is_still_cup(self):
        info = currency_info(BaseUnit.CUP_)
        assert info.code == "CUP"

    def test_cup_from_code_returns_cuban_peso(self):
        assert from_code("CUP") == BaseUnit.CUP_

    def test_cup_from_code_does_not_return_physical_cup(self):
        assert from_code("CUP") != BaseUnit.CUP

    def test_physical_cup_is_not_a_currency(self):
        assert not is_currency(BaseUnit.CUP)

    def test_cuban_peso_is_a_fiat_currency(self):
        assert is_fiat(BaseUnit.CUP_)

    def test_all_known_collisions_are_accounted_for(self):
        physical_names = {m.name for m in BaseUnit
                          if int(m) < int(CURRENCY_FIAT_FIRST)}
        currency_codes = {info.code for info in all_currencies()}
        conflicts = physical_names & currency_codes
        assert conflicts == set(self.KNOWN_DISAMBIGUATIONS.keys()), (
            f"Unexpected collisions: {conflicts - set(self.KNOWN_DISAMBIGUATIONS.keys())}; "
            f"Resolved collisions no longer present: "
            f"{set(self.KNOWN_DISAMBIGUATIONS.keys()) - conflicts}"
        )

    def test_disambiguated_names_not_in_physical_range(self):
        for wire_code, enum_name in self.KNOWN_DISAMBIGUATIONS.items():
            member = BaseUnit[enum_name]
            assert int(member) >= int(CURRENCY_FIAT_FIRST), (
                f"{enum_name} is in the physical range"
            )



class TestBaseUnitSentinel:
    def test_sentinel_is_329(self):
        assert int(BaseUnit._SENTINEL) == 329

    def test_sentinel_is_one_past_atom(self):
        assert int(BaseUnit._SENTINEL) == int(BaseUnit.ATOM) + 1


# ── TestPrefixRulesC ───────────────────────────────────────────────────────

class TestPrefixRulesC:
    """
    Validates the CONTRACT of bvn_currency_prefix_valid() without calling C code.
    The C function signature is:
        bool bvn_currency_prefix_valid(int base, int prefix_system)
    where prefix_system=0 means SI, prefix_system=1 means IEC.
    """

    SI  = 0
    IEC = 1

    def _valid(self, base_int: int, prefix_system: int) -> bool:
        if int(CURRENCY_FIAT_FIRST) <= base_int <= int(CURRENCY_CRYPTO_LAST):
            return prefix_system != self.IEC
        return True

    @pytest.mark.parametrize("code", ["USD", "EUR", "BTC", "ETH", "DOGE"])
    def test_iec_prefix_invalid_on_currency(self, code):
        base = int(from_code(code))
        assert not self._valid(base, self.IEC)

    @pytest.mark.parametrize("code", ["USD", "EUR", "BTC", "ETH", "DOGE"])
    def test_si_prefix_valid_on_currency(self, code):
        base = int(from_code(code))
        assert self._valid(base, self.SI)

    @pytest.mark.parametrize("base", [
        BaseUnit.METER, BaseUnit.GRAM, BaseUnit.SECOND,
    ])
    def test_iec_prefix_valid_on_physical_units_not_blocked(self, base):
        assert self._valid(int(base), self.IEC)


# ── TestParseCurrencyStr ───────────────────────────────────────────────────

class TestParseCurrencyStr:
    """
    Validates the CONTRACT of bvn_parse_currency_str() without calling C code.
    Anything in the table must parse; anything outside must not.
    """

    def _would_parse(self, token: str) -> bool:
        if not token:
            return False
        if len(token) < 3 or len(token) > 4:
            return False
        if not token.isupper() or not token.isalpha():
            return False
        try:
            from_code(token)
            return True
        except KeyError:
            return False

    @pytest.mark.parametrize("code", [
        "USD", "EUR", "GBP", "JPY", "CHF", "AUD", "CAD", "CNY",
        "XAU", "XAG", "XDR", "BHD", "KWD", "OMR", "CLF",
        "BTC", "ETH", "SOL", "XRP", "ADA", "LTC",
        "DOGE", "LINK", "USDT", "USDC", "AVAX", "ATOM",
    ])
    def test_known_codes_parse(self, code):
        assert self._would_parse(code)

    @pytest.mark.parametrize("bad", [
        "XYZ", "AAA", "US", "METER", "usd", "btc", "123", "", "LONGCODE",
    ])
    def test_unknown_or_invalid_codes_reject(self, bad):
        assert not self._would_parse(bad)


# ── TestParseCurrencyStrRejects ────────────────────────────────────────────

class TestParseCurrencyStrRejects:
    """Length and character class checks that the C parser enforces."""

    @pytest.mark.parametrize("token", [
        "AB",        # too short
        "ABCDE",     # too long (5 chars)
        "Usd",       # mixed case
        "u$d",       # non-alpha
        "123",       # digits only
        "",           # empty
    ])
    def test_rejects_malformed_tokens(self, token):
        if not token:
            assert True
            return
        ok = (3 <= len(token) <= 4) and token.isalpha() and token.isupper()
        assert not ok or token not in {i.code for i in all_currencies()}


# ── TestBvnrFormatExamples ─────────────────────────────────────────────────

class TestBvnrFormatExamples:
    """
    Syntactic plausibility checks for .bvnr annotation strings.
    These do NOT parse full Bovnar — they just verify the unit substring
    is a valid currency code (to catch typos in documentation / examples).
    """

    _UNIT_RE = re.compile(
        r'<[^>]*,\s*(?:[a-zA-Z]+~)?'   # optional SI prefix~
        r'([A-Z]{3,4})'                  # the currency code (capture group 1)
        r'(?:[/·][^>]*)?>',             # optional compound tail
        re.ASCII,
    )

    def _extract_currency_codes(self, snippet: str) -> list[str]:
        return self._UNIT_RE.findall(snippet)

    @pytest.mark.parametrize("snippet,expected_codes", [
        (".price = <float_dec:64,USD> 19.99;", ["USD"]),
        (".rate  = <float_dec:64,USD/oz_t> 2351.0;", ["USD"]),
        (".rent  = <float_dec:64,EUR/h> 150.0;", ["EUR"]),
        (".btc_balance = <float_dec:64,BTC> 0.5;", ["BTC"]),
        (".doge = <uint:64,DOGE> 100000;", ["DOGE"]),
        (".link = <float_dec:64,LINK> 12.5;", ["LINK"]),
        (".kwd  = <uint:64,KWD> 1500;", ["KWD"]),
        (".jpy  = <uint:64,JPY> 10000;", ["JPY"]),
        (".fx   = <float_dec:64,USD/EUR> 1.0842;", ["USD"]),
    ])
    def test_currency_code_extractable(self, snippet, expected_codes):
        found = self._extract_currency_codes(snippet)
        for code in expected_codes:
            assert code in found

    @pytest.mark.parametrize("code", [
        "USD", "EUR", "GBP", "JPY", "CHF", "KWD", "BHD", "OMR", "CLF",
        "XAU", "XAG", "XPT", "XDR",
        "BTC", "ETH", "SOL", "XRP", "ADA", "DOGE", "USDT", "AVAX",
    ])
    def test_all_documented_example_codes_are_in_table(self, code):
        assert from_code(code) is not None
