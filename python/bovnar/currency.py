"""
bovnar.currency — Currency and cryptocurrency utilities for Bovnar.

This module provides pure-Python metadata queries over the currency range of
BaseUnit without requiring the shared library.  It is deliberately standalone
so that tools (linters, validators, code generators) can import it without a
libbvnr_shared.so present.

Usage
-----
    from bovnar.currency import (
        is_currency, is_fiat, is_crypto,
        minor_unit, currency_name, currency_code,
        CurrencyInfo, all_fiat, all_crypto,
    )

    print(is_currency(BaseUnit.USD))     # True
    print(is_fiat(BaseUnit.BTC))         # False
    print(is_crypto(BaseUnit.BTC))       # True
    print(minor_unit(BaseUnit.KWD))      # 3
    print(minor_unit(BaseUnit.JPY))      # 0
    print(minor_unit(BaseUnit.ETH))      # 18

    info = currency_info(BaseUnit.EUR)
    print(info.code)         # 'EUR'
    print(info.numeric_code) # 978
    print(info.minor_unit)   # 2
    print(info.is_crypto)    # False
    print(info.name)         # 'Euro'
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterator

from .enums import BaseUnit, CURRENCY_FIAT_FIRST, CURRENCY_FIAT_LAST, \
                   CURRENCY_CRYPTO_FIRST, CURRENCY_CRYPTO_LAST


# ── Metadata record ────────────────────────────────────────────────────────

@dataclass(frozen=True)
class CurrencyInfo:
    """Immutable metadata for one fiat or crypto currency."""
    base:         BaseUnit
    code:         str
    numeric_code: int     # ISO 4217 numeric; 0 for crypto
    minor_unit:   int     # decimal places (exponent N: 1 major = 10^N minor)
    is_crypto:    bool
    name:         str

    @property
    def is_fiat(self) -> bool:
        return not self.is_crypto

    def __str__(self) -> str:
        return self.code


# ── Static metadata table (mirrors bovnar_currency.c) ─────────────────────

_TABLE: dict[BaseUnit, CurrencyInfo] = {}

def _row(base: BaseUnit, code: str, num: int, mu: int,
         crypto: bool, name: str) -> None:
    _TABLE[base] = CurrencyInfo(base, code, num, mu, crypto, name)

# ISO 4217 fiat + precious metals
_row(BaseUnit.AED, "AED", 784, 2, False, "UAE Dirham")
_row(BaseUnit.AFN, "AFN", 971, 2, False, "Afghan Afghani")
_row(BaseUnit.ALL, "ALL",   8, 2, False, "Albanian Lek")
_row(BaseUnit.AMD, "AMD",  51, 2, False, "Armenian Dram")
_row(BaseUnit.ANG, "ANG", 532, 2, False, "Netherlands Antillean Guilder")
_row(BaseUnit.AOA, "AOA", 973, 2, False, "Angolan Kwanza")
_row(BaseUnit.ARS, "ARS",  32, 2, False, "Argentine Peso")
_row(BaseUnit.AUD, "AUD",  36, 2, False, "Australian Dollar")
_row(BaseUnit.AWG, "AWG", 533, 2, False, "Aruban Florin")
_row(BaseUnit.AZN, "AZN", 944, 2, False, "Azerbaijani Manat")
_row(BaseUnit.BAM, "BAM", 977, 2, False, "Bosnia-Herzegovina Convertible Mark")
_row(BaseUnit.BBD, "BBD",  52, 2, False, "Barbados Dollar")
_row(BaseUnit.BDT, "BDT",  50, 2, False, "Bangladeshi Taka")
_row(BaseUnit.BGN, "BGN", 975, 2, False, "Bulgarian Lev")
_row(BaseUnit.BHD, "BHD",  48, 3, False, "Bahraini Dinar")
_row(BaseUnit.BMD, "BMD",  60, 2, False, "Bermudian Dollar")
_row(BaseUnit.BND, "BND",  96, 2, False, "Brunei Dollar")
_row(BaseUnit.BOB, "BOB",  68, 2, False, "Boliviano")
_row(BaseUnit.BRL, "BRL", 986, 2, False, "Brazilian Real")
_row(BaseUnit.BSD, "BSD",  44, 2, False, "Bahamian Dollar")
_row(BaseUnit.BTN, "BTN",  64, 2, False, "Bhutanese Ngultrum")
_row(BaseUnit.BWP, "BWP",  72, 2, False, "Botswana Pula")
_row(BaseUnit.BYN, "BYN", 933, 2, False, "Belarusian Ruble")
_row(BaseUnit.BZD, "BZD",  84, 2, False, "Belize Dollar")
_row(BaseUnit.CAD, "CAD", 124, 2, False, "Canadian Dollar")
_row(BaseUnit.CDF, "CDF", 976, 2, False, "Congolese Franc")
_row(BaseUnit.CHF, "CHF", 756, 2, False, "Swiss Franc")
_row(BaseUnit.CLF, "CLF", 990, 4, False, "Unidad de Fomento")
_row(BaseUnit.CLP, "CLP", 152, 0, False, "Chilean Peso")
_row(BaseUnit.CNY, "CNY", 156, 2, False, "Chinese Yuan")
_row(BaseUnit.COP, "COP", 170, 2, False, "Colombian Peso")
_row(BaseUnit.CRC, "CRC", 188, 2, False, "Costa Rican Colon")
_row(BaseUnit.CUP_, "CUP", 192, 2, False, "Cuban Peso")
_row(BaseUnit.CVE, "CVE", 132, 2, False, "Cape Verdean Escudo")
_row(BaseUnit.CZK, "CZK", 203, 2, False, "Czech Koruna")
_row(BaseUnit.DJF, "DJF", 262, 0, False, "Djiboutian Franc")
_row(BaseUnit.DKK, "DKK", 208, 2, False, "Danish Krone")
_row(BaseUnit.DOP, "DOP", 214, 2, False, "Dominican Peso")
_row(BaseUnit.DZD, "DZD",  12, 2, False, "Algerian Dinar")
_row(BaseUnit.EGP, "EGP", 818, 2, False, "Egyptian Pound")
_row(BaseUnit.ERN, "ERN", 232, 2, False, "Eritrean Nakfa")
_row(BaseUnit.ETB, "ETB", 230, 2, False, "Ethiopian Birr")
_row(BaseUnit.EUR, "EUR", 978, 2, False, "Euro")
_row(BaseUnit.FJD, "FJD", 242, 2, False, "Fijian Dollar")
_row(BaseUnit.FKP, "FKP", 238, 2, False, "Falkland Islands Pound")
_row(BaseUnit.GBP, "GBP", 826, 2, False, "Pound Sterling")
_row(BaseUnit.GEL, "GEL", 981, 2, False, "Georgian Lari")
_row(BaseUnit.GHS, "GHS", 936, 2, False, "Ghanaian Cedi")
_row(BaseUnit.GIP, "GIP", 292, 2, False, "Gibraltar Pound")
_row(BaseUnit.GMD, "GMD", 270, 2, False, "Gambian Dalasi")
_row(BaseUnit.GNF, "GNF", 324, 0, False, "Guinean Franc")
_row(BaseUnit.GTQ, "GTQ", 320, 2, False, "Guatemalan Quetzal")
_row(BaseUnit.GYD, "GYD", 328, 2, False, "Guyanese Dollar")
_row(BaseUnit.HKD, "HKD", 344, 2, False, "Hong Kong Dollar")
_row(BaseUnit.HNL, "HNL", 340, 2, False, "Honduran Lempira")
_row(BaseUnit.HRK, "HRK", 191, 2, False, "Croatian Kuna")
_row(BaseUnit.HTG, "HTG", 332, 2, False, "Haitian Gourde")
_row(BaseUnit.HUF, "HUF", 348, 2, False, "Hungarian Forint")
_row(BaseUnit.IDR, "IDR", 360, 2, False, "Indonesian Rupiah")
_row(BaseUnit.ILS, "ILS", 376, 2, False, "Israeli New Shekel")
_row(BaseUnit.INR, "INR", 356, 2, False, "Indian Rupee")
_row(BaseUnit.IQD, "IQD", 368, 3, False, "Iraqi Dinar")
_row(BaseUnit.IRR, "IRR", 364, 2, False, "Iranian Rial")
_row(BaseUnit.ISK, "ISK", 352, 0, False, "Icelandic Krona")
_row(BaseUnit.JMD, "JMD", 388, 2, False, "Jamaican Dollar")
_row(BaseUnit.JOD, "JOD", 400, 3, False, "Jordanian Dinar")
_row(BaseUnit.JPY, "JPY", 392, 0, False, "Japanese Yen")
_row(BaseUnit.KES, "KES", 404, 2, False, "Kenyan Shilling")
_row(BaseUnit.KGS, "KGS", 417, 2, False, "Kyrgystani Som")
_row(BaseUnit.KHR, "KHR", 116, 2, False, "Cambodian Riel")
_row(BaseUnit.KMF, "KMF", 174, 0, False, "Comorian Franc")
_row(BaseUnit.KPW, "KPW", 408, 2, False, "North Korean Won")
_row(BaseUnit.KRW, "KRW", 410, 0, False, "South Korean Won")
_row(BaseUnit.KWD, "KWD", 414, 3, False, "Kuwaiti Dinar")
_row(BaseUnit.KYD, "KYD", 136, 2, False, "Cayman Islands Dollar")
_row(BaseUnit.KZT, "KZT", 398, 2, False, "Kazakhstani Tenge")
_row(BaseUnit.LAK, "LAK", 418, 2, False, "Laotian Kip")
_row(BaseUnit.LBP, "LBP", 422, 2, False, "Lebanese Pound")
_row(BaseUnit.LKR, "LKR", 144, 2, False, "Sri Lankan Rupee")
_row(BaseUnit.LRD, "LRD", 430, 2, False, "Liberian Dollar")
_row(BaseUnit.LSL, "LSL", 426, 2, False, "Lesotho Loti")
_row(BaseUnit.LYD, "LYD", 434, 3, False, "Libyan Dinar")
_row(BaseUnit.MAD, "MAD", 504, 2, False, "Moroccan Dirham")
_row(BaseUnit.MDL, "MDL", 498, 2, False, "Moldovan Leu")
_row(BaseUnit.MGA, "MGA", 969, 2, False, "Malagasy Ariary")
_row(BaseUnit.MKD, "MKD", 807, 2, False, "Macedonian Denar")
_row(BaseUnit.MMK, "MMK", 104, 2, False, "Myanmar Kyat")
_row(BaseUnit.MNT, "MNT", 496, 2, False, "Mongolian Togrog")
_row(BaseUnit.MOP, "MOP", 446, 2, False, "Macanese Pataca")
_row(BaseUnit.MRU, "MRU", 929, 2, False, "Mauritanian Ouguiya")
_row(BaseUnit.MUR, "MUR", 480, 2, False, "Mauritian Rupee")
_row(BaseUnit.MVR, "MVR", 462, 2, False, "Maldivian Rufiyaa")
_row(BaseUnit.MWK, "MWK", 454, 2, False, "Malawian Kwacha")
_row(BaseUnit.MXN, "MXN", 484, 2, False, "Mexican Peso")
_row(BaseUnit.MYR, "MYR", 458, 2, False, "Malaysian Ringgit")
_row(BaseUnit.MZN, "MZN", 943, 2, False, "Mozambican Metical")
_row(BaseUnit.NAD, "NAD", 516, 2, False, "Namibian Dollar")
_row(BaseUnit.NGN, "NGN", 566, 2, False, "Nigerian Naira")
_row(BaseUnit.NIO, "NIO", 558, 2, False, "Nicaraguan Cordoba")
_row(BaseUnit.NOK, "NOK", 578, 2, False, "Norwegian Krone")
_row(BaseUnit.NPR, "NPR", 524, 2, False, "Nepalese Rupee")
_row(BaseUnit.NZD, "NZD", 554, 2, False, "New Zealand Dollar")
_row(BaseUnit.OMR, "OMR", 512, 3, False, "Omani Rial")
_row(BaseUnit.PAB, "PAB", 590, 2, False, "Panamanian Balboa")
_row(BaseUnit.PEN, "PEN", 604, 2, False, "Peruvian Sol")
_row(BaseUnit.PGK, "PGK", 598, 2, False, "Papua New Guinean Kina")
_row(BaseUnit.PHP, "PHP", 608, 2, False, "Philippine Peso")
_row(BaseUnit.PKR, "PKR", 586, 2, False, "Pakistani Rupee")
_row(BaseUnit.PLN, "PLN", 985, 2, False, "Polish Zloty")
_row(BaseUnit.PYG, "PYG", 600, 0, False, "Paraguayan Guarani")
_row(BaseUnit.QAR, "QAR", 634, 2, False, "Qatari Riyal")
_row(BaseUnit.RON, "RON", 946, 2, False, "Romanian Leu")
_row(BaseUnit.RSD, "RSD", 941, 2, False, "Serbian Dinar")
_row(BaseUnit.RUB, "RUB", 643, 2, False, "Russian Ruble")
_row(BaseUnit.RWF, "RWF", 646, 0, False, "Rwandan Franc")
_row(BaseUnit.SAR, "SAR", 682, 2, False, "Saudi Riyal")
_row(BaseUnit.SBD, "SBD",  90, 2, False, "Solomon Islands Dollar")
_row(BaseUnit.SCR, "SCR", 690, 2, False, "Seychellois Rupee")
_row(BaseUnit.SDG, "SDG", 938, 2, False, "Sudanese Pound")
_row(BaseUnit.SEK, "SEK", 752, 2, False, "Swedish Krona")
_row(BaseUnit.SGD, "SGD", 702, 2, False, "Singapore Dollar")
_row(BaseUnit.SHP, "SHP", 654, 2, False, "Saint Helena Pound")
_row(BaseUnit.SLL, "SLL", 694, 2, False, "Sierra Leonean Leone")
_row(BaseUnit.SOS, "SOS", 706, 2, False, "Somali Shilling")
_row(BaseUnit.SRD, "SRD", 968, 2, False, "Surinamese Dollar")
_row(BaseUnit.STN, "STN", 930, 2, False, "Sao Tome and Principe Dobra")
_row(BaseUnit.SVC, "SVC", 222, 2, False, "Salvadoran Colon")
_row(BaseUnit.SYP, "SYP", 760, 2, False, "Syrian Pound")
_row(BaseUnit.SZL, "SZL", 748, 2, False, "Swazi Lilangeni")
_row(BaseUnit.THB, "THB", 764, 2, False, "Thai Baht")
_row(BaseUnit.TJS, "TJS", 972, 2, False, "Tajikistani Somoni")
_row(BaseUnit.TMT, "TMT", 934, 2, False, "Turkmenistan Manat")
_row(BaseUnit.TND, "TND", 788, 3, False, "Tunisian Dinar")
_row(BaseUnit.TOP, "TOP", 776, 2, False, "Tongan Pa'anga")
_row(BaseUnit.TRY, "TRY", 949, 2, False, "Turkish Lira")
_row(BaseUnit.TTD, "TTD", 780, 2, False, "Trinidad and Tobago Dollar")
_row(BaseUnit.TWD, "TWD", 901, 2, False, "New Taiwan Dollar")
_row(BaseUnit.TZS, "TZS", 834, 2, False, "Tanzanian Shilling")
_row(BaseUnit.UAH, "UAH", 980, 2, False, "Ukrainian Hryvnia")
_row(BaseUnit.UGX, "UGX", 800, 0, False, "Ugandan Shilling")
_row(BaseUnit.USD, "USD", 840, 2, False, "US Dollar")
_row(BaseUnit.UYU, "UYU", 858, 2, False, "Uruguayan Peso")
_row(BaseUnit.UZS, "UZS", 860, 2, False, "Uzbekistani Som")
_row(BaseUnit.VES, "VES", 928, 2, False, "Venezuelan Bolivar Soberano")
_row(BaseUnit.VND, "VND", 704, 0, False, "Vietnamese Dong")
_row(BaseUnit.VUV, "VUV", 548, 0, False, "Vanuatu Vatu")
_row(BaseUnit.WST, "WST", 882, 2, False, "Samoan Tala")
_row(BaseUnit.XAF, "XAF", 950, 0, False, "CFA Franc BEAC")
_row(BaseUnit.XAG, "XAG", 961, 0, False, "Silver")
_row(BaseUnit.XAU, "XAU", 959, 0, False, "Gold")
_row(BaseUnit.XCD, "XCD", 951, 2, False, "East Caribbean Dollar")
_row(BaseUnit.XDR, "XDR", 960, 0, False, "Special Drawing Rights")
_row(BaseUnit.XOF, "XOF", 952, 0, False, "CFA Franc BCEAO")
_row(BaseUnit.XPD, "XPD", 964, 0, False, "Palladium")
_row(BaseUnit.XPF, "XPF", 953, 0, False, "CFP Franc")
_row(BaseUnit.XPT, "XPT", 962, 0, False, "Platinum")
_row(BaseUnit.XTS, "XTS", 963, 0, False, "Test")
_row(BaseUnit.YER, "YER", 886, 2, False, "Yemeni Rial")
_row(BaseUnit.ZAR, "ZAR", 710, 2, False, "South African Rand")
_row(BaseUnit.ZMW, "ZMW", 967, 2, False, "Zambian Kwacha")
_row(BaseUnit.ZWL, "ZWL", 932, 2, False, "Zimbabwean Dollar")

# Cryptocurrencies
_row(BaseUnit.BTC,  "BTC",  0,  8, True, "Bitcoin")
_row(BaseUnit.ETH,  "ETH",  0, 18, True, "Ethereum")
_row(BaseUnit.SOL,  "SOL",  0,  9, True, "Solana")
_row(BaseUnit.XRP,  "XRP",  0,  6, True, "XRP")
_row(BaseUnit.BNB,  "BNB",  0, 18, True, "BNB")
_row(BaseUnit.ADA,  "ADA",  0,  6, True, "Cardano")
_row(BaseUnit.LTC,  "LTC",  0,  8, True, "Litecoin")
_row(BaseUnit.DOT,  "DOT",  0, 10, True, "Polkadot")
_row(BaseUnit.XMR,  "XMR",  0, 12, True, "Monero")
_row(BaseUnit.ETC,  "ETC",  0, 18, True, "Ethereum Classic")
_row(BaseUnit.BCH,  "BCH",  0,  8, True, "Bitcoin Cash")
_row(BaseUnit.XLM,  "XLM",  0,  7, True, "Stellar")
_row(BaseUnit.FIL,  "FIL",  0, 18, True, "Filecoin")
_row(BaseUnit.ICP,  "ICP",  0,  8, True, "Internet Computer")
_row(BaseUnit.TRX,  "TRX",  0,  6, True, "TRON")
_row(BaseUnit.EOS,  "EOS",  0,  4, True, "EOS")
_row(BaseUnit.VET,  "VET",  0, 18, True, "VeChain")
_row(BaseUnit.NEO,  "NEO",  0,  8, True, "Neo")
_row(BaseUnit.ZEC,  "ZEC",  0,  8, True, "Zcash")
_row(BaseUnit.UNI,  "UNI",  0, 18, True, "Uniswap")
_row(BaseUnit.ARB,  "ARB",  0, 18, True, "Arbitrum")
_row(BaseUnit.SUI,  "SUI",  0,  9, True, "Sui")
_row(BaseUnit.TON,  "TON",  0,  9, True, "Toncoin")
_row(BaseUnit.INJ,  "INJ",  0, 18, True, "Injective")
_row(BaseUnit.SEI,  "SEI",  0,  6, True, "Sei")
_row(BaseUnit.APT,  "APT",  0,  8, True, "Aptos")
_row(BaseUnit.TAO,  "TAO",  0,  9, True, "Bittensor")
_row(BaseUnit.WIF,  "WIF",  0,  6, True, "dogwifhat")
_row(BaseUnit.DOGE, "DOGE", 0,  8, True, "Dogecoin")
_row(BaseUnit.LINK, "LINK", 0, 18, True, "Chainlink")
_row(BaseUnit.USDT, "USDT", 0,  6, True, "Tether")
_row(BaseUnit.USDC, "USDC", 0,  6, True, "USD Coin")
_row(BaseUnit.AVAX, "AVAX", 0, 18, True, "Avalanche")
_row(BaseUnit.ATOM, "ATOM", 0,  6, True, "Cosmos")


# ── Public predicates ──────────────────────────────────────────────────────

def is_currency(base: BaseUnit) -> bool:
    """True if base is any fiat, precious metal, or crypto currency."""
    return int(CURRENCY_FIAT_FIRST) <= int(base) <= int(CURRENCY_CRYPTO_LAST)


def is_fiat(base: BaseUnit) -> bool:
    """True if base is an ISO 4217 fiat currency or precious metal."""
    return int(CURRENCY_FIAT_FIRST) <= int(base) <= int(CURRENCY_FIAT_LAST)


def is_crypto(base: BaseUnit) -> bool:
    """True if base is a cryptocurrency."""
    return int(CURRENCY_CRYPTO_FIRST) <= int(base) <= int(CURRENCY_CRYPTO_LAST)


def currency_info(base: BaseUnit) -> CurrencyInfo:
    """
    Return the CurrencyInfo record for *base*.
    Raises KeyError if *base* is not a currency unit.
    """
    if base not in _TABLE:
        raise KeyError(f"{base!r} is not a currency BaseUnit")
    return _TABLE[base]


def minor_unit(base: BaseUnit) -> int:
    """
    Return the minor-unit exponent N (1 major unit = 10^N minor units).
    E.g. minor_unit(USD)=2, minor_unit(JPY)=0, minor_unit(KWD)=3,
         minor_unit(BTC)=8, minor_unit(ETH)=18.
    Raises KeyError if *base* is not a currency.
    """
    return currency_info(base).minor_unit


def currency_name(base: BaseUnit) -> str:
    """Return the human-readable name for *base*."""
    return currency_info(base).name


def currency_code(base: BaseUnit) -> str:
    """Return the ISO 4217 or conventional ticker code for *base*."""
    return currency_info(base).code


def from_code(code: str) -> BaseUnit:
    """
    Reverse lookup: return the BaseUnit for an ISO 4217 / ticker code string.
    Raises KeyError if the code is unknown.
    """
    upper = code.strip().upper()
    for info in _TABLE.values():
        if info.code == upper:
            return info.base
    raise KeyError(f"Unknown currency code: {code!r}")


def all_fiat() -> Iterator[CurrencyInfo]:
    """Yield CurrencyInfo for every fiat currency / precious metal."""
    for base, info in _TABLE.items():
        if not info.is_crypto:
            yield info


def all_crypto() -> Iterator[CurrencyInfo]:
    """Yield CurrencyInfo for every cryptocurrency."""
    for base, info in _TABLE.items():
        if info.is_crypto:
            yield info


def all_currencies() -> Iterator[CurrencyInfo]:
    """Yield CurrencyInfo for every currency (fiat + crypto)."""
    yield from _TABLE.values()
