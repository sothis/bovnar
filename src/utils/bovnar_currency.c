/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Janos Sonntag
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "bovnar_currency.h"
#include <stddef.h>
#include <string.h>
#define N_FIAT   (BVN_CURRENCY_FIAT_LAST  - BVN_CURRENCY_FIAT_FIRST  + 1)
#define N_CRYPTO (BVN_CURRENCY_CRYPTO_LAST - BVN_CURRENCY_CRYPTO_FIRST + 1)
#define N_TOTAL  (N_FIAT + N_CRYPTO)
static const bvn_currency_info_t g_currency_table[N_TOTAL] = {
  { "AED", 784, 2, false, "UAE Dirham" },
  { "AFN", 971, 2, false, "Afghan Afghani" },
  { "ALL",   8, 2, false, "Albanian Lek" },
  { "AMD",  51, 2, false, "Armenian Dram" },
  { "ANG", 532, 2, false, "Netherlands Antillean Guilder" },
  { "AOA", 973, 2, false, "Angolan Kwanza" },
  { "ARS",  32, 2, false, "Argentine Peso" },
  { "AUD",  36, 2, false, "Australian Dollar" },
  { "AWG", 533, 2, false, "Aruban Florin" },
  { "AZN", 944, 2, false, "Azerbaijani Manat" },
  { "BAM", 977, 2, false, "Bosnia-Herzegovina Convertible Mark" },
  { "BBD",  52, 2, false, "Barbados Dollar" },
  { "BDT",  50, 2, false, "Bangladeshi Taka" },
  { "BGN", 975, 2, false, "Bulgarian Lev" },
  { "BHD",  48, 3, false, "Bahraini Dinar" },
  { "BIF", 108, 0, false, "Burundian Franc" },
  { "BMD",  60, 2, false, "Bermudian Dollar" },
  { "BND",  96, 2, false, "Brunei Dollar" },
  { "BOB",  68, 2, false, "Boliviano" },
  { "BRL", 986, 2, false, "Brazilian Real" },
  { "BSD",  44, 2, false, "Bahamian Dollar" },
  { "BTN",  64, 2, false, "Bhutanese Ngultrum" },
  { "BWP",  72, 2, false, "Botswana Pula" },
  { "BYN", 933, 2, false, "Belarusian Ruble" },
  { "BZD",  84, 2, false, "Belize Dollar" },
  { "CAD", 124, 2, false, "Canadian Dollar" },
  { "CDF", 976, 2, false, "Congolese Franc" },
  { "CHF", 756, 2, false, "Swiss Franc" },
  { "CLF", 990, 4, false, "Unidad de Fomento" },
  { "CLP", 152, 0, false, "Chilean Peso" },
  { "CNY", 156, 2, false, "Chinese Yuan" },
  { "COP", 170, 2, false, "Colombian Peso" },
  { "CRC", 188, 2, false, "Costa Rican Colon" },
  { "CUP", 192, 2, false, "Cuban Peso" },
  { "CVE", 132, 2, false, "Cape Verdean Escudo" },
  { "CZK", 203, 2, false, "Czech Koruna" },
  { "DJF", 262, 0, false, "Djiboutian Franc" },
  { "DKK", 208, 2, false, "Danish Krone" },
  { "DOP", 214, 2, false, "Dominican Peso" },
  { "DZD",  12, 2, false, "Algerian Dinar" },
  { "EGP", 818, 2, false, "Egyptian Pound" },
  { "ERN", 232, 2, false, "Eritrean Nakfa" },
  { "ETB", 230, 2, false, "Ethiopian Birr" },
  { "EUR", 978, 2, false, "Euro" },
  { "FJD", 242, 2, false, "Fijian Dollar" },
  { "FKP", 238, 2, false, "Falkland Islands Pound" },
  { "GBP", 826, 2, false, "Pound Sterling" },
  { "GEL", 981, 2, false, "Georgian Lari" },
  { "GHS", 936, 2, false, "Ghanaian Cedi" },
  { "GIP", 292, 2, false, "Gibraltar Pound" },
  { "GMD", 270, 2, false, "Gambian Dalasi" },
  { "GNF", 324, 0, false, "Guinean Franc" },
  { "GTQ", 320, 2, false, "Guatemalan Quetzal" },
  { "GYD", 328, 2, false, "Guyanese Dollar" },
  { "HKD", 344, 2, false, "Hong Kong Dollar" },
  { "HNL", 340, 2, false, "Honduran Lempira" },
  { "HRK", 191, 2, false, "Croatian Kuna" },
  { "HTG", 332, 2, false, "Haitian Gourde" },
  { "HUF", 348, 2, false, "Hungarian Forint" },
  { "IDR", 360, 2, false, "Indonesian Rupiah" },
  { "ILS", 376, 2, false, "Israeli New Shekel" },
  { "INR", 356, 2, false, "Indian Rupee" },
  { "IQD", 368, 3, false, "Iraqi Dinar" },
  { "IRR", 364, 2, false, "Iranian Rial" },
  { "ISK", 352, 0, false, "Icelandic Krona" },
  { "JMD", 388, 2, false, "Jamaican Dollar" },
  { "JOD", 400, 3, false, "Jordanian Dinar" },
  { "JPY", 392, 0, false, "Japanese Yen" },
  { "KES", 404, 2, false, "Kenyan Shilling" },
  { "KGS", 417, 2, false, "Kyrgystani Som" },
  { "KHR", 116, 2, false, "Cambodian Riel" },
  { "KMF", 174, 0, false, "Comorian Franc" },
  { "KPW", 408, 2, false, "North Korean Won" },
  { "KRW", 410, 0, false, "South Korean Won" },
  { "KWD", 414, 3, false, "Kuwaiti Dinar" },
  { "KYD", 136, 2, false, "Cayman Islands Dollar" },
  { "KZT", 398, 2, false, "Kazakhstani Tenge" },
  { "LAK", 418, 2, false, "Laotian Kip" },
  { "LBP", 422, 2, false, "Lebanese Pound" },
  { "LKR", 144, 2, false, "Sri Lankan Rupee" },
  { "LRD", 430, 2, false, "Liberian Dollar" },
  { "LSL", 426, 2, false, "Lesotho Loti" },
  { "LYD", 434, 3, false, "Libyan Dinar" },
  { "MAD", 504, 2, false, "Moroccan Dirham" },
  { "MDL", 498, 2, false, "Moldovan Leu" },
  { "MGA", 969, 2, false, "Malagasy Ariary" },
  { "MKD", 807, 2, false, "Macedonian Denar" },
  { "MMK", 104, 2, false, "Myanmar Kyat" },
  { "MNT", 496, 2, false, "Mongolian Togrog" },
  { "MOP", 446, 2, false, "Macanese Pataca" },
  { "MRU", 929, 2, false, "Mauritanian Ouguiya" },
  { "MUR", 480, 2, false, "Mauritian Rupee" },
  { "MVR", 462, 2, false, "Maldivian Rufiyaa" },
  { "MWK", 454, 2, false, "Malawian Kwacha" },
  { "MXN", 484, 2, false, "Mexican Peso" },
  { "MYR", 458, 2, false, "Malaysian Ringgit" },
  { "MZN", 943, 2, false, "Mozambican Metical" },
  { "NAD", 516, 2, false, "Namibian Dollar" },
  { "NGN", 566, 2, false, "Nigerian Naira" },
  { "NIO", 558, 2, false, "Nicaraguan Cordoba" },
  { "NOK", 578, 2, false, "Norwegian Krone" },
  { "NPR", 524, 2, false, "Nepalese Rupee" },
  { "NZD", 554, 2, false, "New Zealand Dollar" },
  { "OMR", 512, 3, false, "Omani Rial" },
  { "PAB", 590, 2, false, "Panamanian Balboa" },
  { "PEN", 604, 2, false, "Peruvian Sol" },
  { "PGK", 598, 2, false, "Papua New Guinean Kina" },
  { "PHP", 608, 2, false, "Philippine Peso" },
  { "PKR", 586, 2, false, "Pakistani Rupee" },
  { "PLN", 985, 2, false, "Polish Zloty" },
  { "PYG", 600, 0, false, "Paraguayan Guarani" },
  { "QAR", 634, 2, false, "Qatari Riyal" },
  { "RON", 946, 2, false, "Romanian Leu" },
  { "RSD", 941, 2, false, "Serbian Dinar" },
  { "RUB", 643, 2, false, "Russian Ruble" },
  { "RWF", 646, 0, false, "Rwandan Franc" },
  { "SAR", 682, 2, false, "Saudi Riyal" },
  { "SBD",  90, 2, false, "Solomon Islands Dollar" },
  { "SCR", 690, 2, false, "Seychellois Rupee" },
  { "SDG", 938, 2, false, "Sudanese Pound" },
  { "SEK", 752, 2, false, "Swedish Krona" },
  { "SGD", 702, 2, false, "Singapore Dollar" },
  { "SHP", 654, 2, false, "Saint Helena Pound" },
  { "SLE", 925, 2, false, "Sierra Leonean Leone" },
  { "SLL", 694, 2, false, "Sierra Leonean Leone (old)" },
  { "SOS", 706, 2, false, "Somali Shilling" },
  { "SSP", 728, 2, false, "South Sudanese Pound" },
  { "SRD", 968, 2, false, "Surinamese Dollar" },
  { "STN", 930, 2, false, "Sao Tome and Principe Dobra" },
  { "SVC", 222, 2, false, "Salvadoran Colon" },
  { "SYP", 760, 2, false, "Syrian Pound" },
  { "SZL", 748, 2, false, "Swazi Lilangeni" },
  { "THB", 764, 2, false, "Thai Baht" },
  { "TJS", 972, 2, false, "Tajikistani Somoni" },
  { "TMT", 934, 2, false, "Turkmenistan Manat" },
  { "TND", 788, 3, false, "Tunisian Dinar" },
  { "TOP", 776, 2, false, "Tongan Pa'anga" },
  { "TRY", 949, 2, false, "Turkish Lira" },
  { "TTD", 780, 2, false, "Trinidad and Tobago Dollar" },
  { "TWD", 901, 2, false, "New Taiwan Dollar" },
  { "TZS", 834, 2, false, "Tanzanian Shilling" },
  { "UAH", 980, 2, false, "Ukrainian Hryvnia" },
  { "UGX", 800, 0, false, "Ugandan Shilling" },
  { "USD", 840, 2, false, "US Dollar" },
  { "UYU", 858, 2, false, "Uruguayan Peso" },
  { "UZS", 860, 2, false, "Uzbekistani Som" },
  { "VES", 928, 2, false, "Venezuelan Bolivar Soberano" },
  { "VND", 704, 0, false, "Vietnamese Dong" },
  { "VUV", 548, 0, false, "Vanuatu Vatu" },
  { "WST", 882, 2, false, "Samoan Tala" },
  { "XAF", 950, 0, false, "CFA Franc BEAC" },
  { "XAG", 961, 0, false, "Silver" },
  { "XAU", 959, 0, false, "Gold" },
  { "XCD", 951, 2, false, "East Caribbean Dollar" },
  { "XDR", 960, 0, false, "Special Drawing Rights" },
  { "XOF", 952, 0, false, "CFA Franc BCEAO" },
  { "XPD", 964, 0, false, "Palladium" },
  { "XPF", 953, 0, false, "CFP Franc" },
  { "XPT", 962, 0, false, "Platinum" },
  { "XTS", 963, 0, false, "Test" },
  { "YER", 886, 2, false, "Yemeni Rial" },
  { "ZAR", 710, 2, false, "South African Rand" },
  { "ZMW", 967, 2, false, "Zambian Kwacha" },
  { "ZWL", 932, 2, false, "Zimbabwean Dollar" },
  { "BTC",   0,  8, true,  "Bitcoin" },
  { "ETH",   0, 18, true,  "Ethereum" },
  { "SOL",   0,  9, true,  "Solana" },
  { "XRP",   0,  6, true,  "XRP" },
  { "BNB",   0, 18, true,  "BNB" },
  { "ADA",   0,  6, true,  "Cardano" },
  { "LTC",   0,  8, true,  "Litecoin" },
  { "DOT",   0, 10, true,  "Polkadot" },
  { "XMR",   0, 12, true,  "Monero" },
  { "ETC",   0, 18, true,  "Ethereum Classic" },
  { "BCH",   0,  8, true,  "Bitcoin Cash" },
  { "XLM",   0,  7, true,  "Stellar" },
  { "FIL",   0, 18, true,  "Filecoin" },
  { "ICP",   0,  8, true,  "Internet Computer" },
  { "TRX",   0,  6, true,  "TRON" },
  { "EOS",   0,  4, true,  "EOS" },
  { "VET",   0, 18, true,  "VeChain" },
  { "NEO",   0,  8, true,  "Neo" },
  { "ZEC",   0,  8, true,  "Zcash" },
  { "UNI",   0, 18, true,  "Uniswap" },
  { "ARB",   0, 18, true,  "Arbitrum" },
  { "SUI",   0,  9, true,  "Sui" },
  { "TON",   0,  9, true,  "Toncoin" },
  { "INJ",   0, 18, true,  "Injective" },
  { "SEI",   0,  6, true,  "Sei" },
  { "APT",   0,  8, true,  "Aptos" },
  { "TAO",   0,  9, true,  "Bittensor" },
  { "WIF",   0,  6, true,  "dogwifhat" },
  { "DOGE",  0,  8, true,  "Dogecoin" },
  { "LINK",  0, 18, true,  "Chainlink" },
  { "USDT",  0,  6, true,  "Tether" },
  { "USDC",  0,  6, true,  "USD Coin" },
  { "AVAX",  0, 18, true,  "Avalanche" },
  { "ATOM",  0,  6, true,  "Cosmos" },
  { "POL",   0, 18, true,  "Polygon" },
  { "NEAR",  0, 24, true,  "NEAR Protocol" },
  { "ALGO",  0,  6, true,  "Algorand" },
  { "HBAR",  0,  8, true,  "Hedera" },
  { "AAVE",  0, 18, true,  "Aave" },
  { "MKR",   0, 18, true,  "Maker" },
  { "DAI",   0, 18, true,  "Dai" },
  { "STX",   0,  6, true,  "Stacks" },
  { "GRT",   0, 18, true,  "The Graph" },
  { "LDO",   0, 18, true,  "Lido DAO" },
  { "BONK",  0,  5, true,  "Bonk" },
  { "PEPE",  0, 18, true,  "Pepe" },
  { "SHIB",  0, 18, true,  "Shiba Inu" },
  { "JUP",   0,  6, true,  "Jupiter" },
  { "PYTH",  0,  6, true,  "Pyth Network" },
  { "RUNE",  0,  8, true,  "THORChain" },
};
bool bvn_unit_is_currency(int base)
{
    return (base >= BVN_CURRENCY_FIAT_FIRST && base <= BVN_CURRENCY_CRYPTO_LAST);
}
bool bvn_unit_is_fiat(int base)
{
    return (base >= BVN_CURRENCY_FIAT_FIRST && base <= BVN_CURRENCY_FIAT_LAST);
}
bool bvn_unit_is_crypto(int base)
{
    return (base >= BVN_CURRENCY_CRYPTO_FIRST && base <= BVN_CURRENCY_CRYPTO_LAST);
}
uint8_t bvn_currency_minor_unit(int base, bool *ok)
{
    if (!bvn_unit_is_currency(base)) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return g_currency_table[base - BVN_CURRENCY_FIAT_FIRST].minor_unit;
}
const bvn_currency_info_t *bvn_currency_info(int base)
{
    if (!bvn_unit_is_currency(base))
        return NULL;
    return &g_currency_table[base - BVN_CURRENCY_FIAT_FIRST];
}
int bvn_parse_currency_str(const uint8_t *s, uint32_t len)
{
    if (!s || len < 3 || len > 4)
        return 0;
    for (uint32_t i = 0; i < len; i++) {
        if (s[i] < 'A' || s[i] > 'Z')
            return 0;
    }
    for (int idx = 0; idx < N_TOTAL; idx++) {
        const char *code = g_currency_table[idx].code;
        if ((uint32_t)strlen(code) == len && memcmp(code, s, len) == 0)
            return idx + BVN_CURRENCY_FIAT_FIRST;
    }
    return 0;
}
bool bvn_currency_prefix_valid(int base, int prefix_system)
{
    if (!bvn_unit_is_currency(base))
        return true;
    return (prefix_system != 1);
}
