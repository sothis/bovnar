# Bovnar Currency Unit Extension

> **Status:** SUPERSEDED — all content has been merged into
> `doc/2_bovnar_unit_system.md` (§9, §10, §12.5, §12.6, §14, §15.4).
> This file is retained for git history only and should not be edited.
> **Spec version:** 1.1 (currency extension)

---

## 12. Currency Units

### 12.1 Overview

Currency amounts are dimensional quantities in financial computing.  `$19.99 USD` carries a denomination dimension the same way that `9.81 m/s²` carries an acceleration dimension.  Bovnar extends the unit system with 195 currency and cryptocurrency codes so that monetary data can be annotated, validated, and round-tripped with the same precision guarantees as physical measurements.

### 12.2 Namespace Rule

In Bovnar's unit grammar, any token consisting **exclusively of uppercase ASCII letters with length 3 or 4** is permanently reserved for currency codes.  No physical base unit uses such a symbol; all existing physical units are either single-letter (`m`, `K`, `A`…), mixed-case (`Hz`, `Pa`, `Wb`…), or lowercase-dominated (`mol`, `min`, `bar`…).

This reservation requires no new sigil character and no change to the unit character set.  Classification happens at the lookup stage: after failing the physical-unit table, the parser checks whether the accumulated token is all-uppercase 3–4 chars and dispatches to the currency table.

### 12.3 Supported Codes

#### 12.3.1 ISO 4217 Fiat Currencies and Precious Metals

All 161 active ISO 4217 alphabetic codes are supported.  Base enum values are assigned alphabetically, beginning at `bu_aed = 134` and ending at `bu_zwl = 294`.

Key minor-unit values (exponent N: 1 major unit = 10^N minor units):

| Code | Name | Minor unit |
|------|------|-----------|
| `JPY` | Japanese Yen | 0 |
| `KRW` | South Korean Won | 0 |
| `VND` | Vietnamese Dong | 0 |
| `ISK` | Icelandic Króna | 0 |
| `USD` | US Dollar | 2 |
| `EUR` | Euro | 2 |
| `GBP` | Pound Sterling | 2 |
| `BHD` | Bahraini Dinar | 3 |
| `JOD` | Jordanian Dinar | 3 |
| `KWD` | Kuwaiti Dinar | 3 |
| `OMR` | Omani Rial | 3 |
| `TND` | Tunisian Dinar | 3 |
| `CLF` | Unidad de Fomento | 4 |
| `XAU` | Gold | 0 |
| `XAG` | Silver | 0 |
| `XPT` | Platinum | 0 |
| `XPD` | Palladium | 0 |
| `XDR` | Special Drawing Rights | 0 |

#### 12.3.2 Cryptocurrencies

34 cryptocurrencies are supported, with 3- or 4-letter uppercase tickers.  Base enum values begin at `bu_btc = 295`.  The `minor_unit` field holds the canonical on-chain decimal places.

| Code | Name | Minor unit | Subunit |
|------|------|-----------|---------|
| `BTC` | Bitcoin | 8 | satoshi |
| `ETH` | Ethereum | 18 | wei |
| `SOL` | Solana | 9 | lamport |
| `XRP` | XRP | 6 | drop |
| `ADA` | Cardano | 6 | lovelace |
| `DOT` | Polkadot | 10 | planck |
| `XMR` | Monero | 12 | piconero |
| `XLM` | Stellar | 7 | stroop |
| `DOGE` | Dogecoin | 8 | koinu |
| `USDT` | Tether | 6 | — |
| `USDC` | USD Coin | 6 | — |
| `AVAX` | Avalanche | 18 | — |
| `ATOM` | Cosmos | 6 | — |

### 12.4 Prefix Rules

**SI prefixes** are permitted on all currency units.  `k~USD` denotes "values in thousands of USD" — a common scale annotation in financial reporting.  The `~` separator follows the existing convention for all prefixed units.

```bovnar
.fund_nav   = <float_dec:64,k~USD>    250.0;   # $250,000
.gdp        = <float_dec:64,M~EUR> 42800.0;   # €42.8 billion
.eth_gwei   = <float_dec:64,G~ETH>    35.0;   # 35 Gwei gas price
```

**IEC binary prefixes** (`Ki`, `Mi`, `Gi`, …) are **forbidden** on all currency units.  `bvn_prefix_unit_valid()` returns `false` for any IEC + currency combination; the parser raises `error_unit_illegal`.

### 12.5 Compound Unit Composition

Currency codes participate in compound unit expressions using the existing separators (`·`, `*`, `/`).

```bovnar
.gold_spot    = <float_dec:64,USD/oz_t>    2351.40;  # $/troy oz
.wheat        = <float_dec:64,USD/bsh>        5.82;  # $/bushel
.rent         = <float_dec:64,EUR/m²>        12.50;  # €/m²
.billing_rate = <float_dec:64,EUR/h>         150.00;  # €/h
.eur_usd      = <float_dec:64,USD/EUR>        1.0842; # exchange rate
.eth_btc      = <float_dec:64,BTC/ETH>       0.05610; # cross-crypto rate
```

Currency × currency compounds (`USD·EUR`) are syntactically valid and produce no error.  Their financial interpretation is the application's responsibility, consistent with Bovnar's policy of validating form and type, not economic semantics.

### 12.6 Compatibility Rules

`bvn_units_compatible()` requires no modification.  Two unit expressions involving currencies are structurally compatible only if they have identical component sequences, including base enum values.  Since `bu_usd ≠ bu_eur`, `USD` and `EUR` are already structurally incompatible under the existing logic.

### 12.7 Type Pairing Recommendations

| Use case | Recommended annotation | Rationale |
|---|---|---|
| Decimal monetary amount | `<float_dec:64,USD>` | Exact decimal; 16 sig. digits |
| High-precision / actuarial | `<float_dec:128,USD>` | 34 sig. digits |
| Integer minor-unit storage | `<uint:64,USD>` | Value in cents; application reads `minor_unit()` |
| Negative balances in minor units | `<sint:64,USD>` | |
| Zero-minor-unit currency | `<uint:64,JPY>` | Integer is the only correct choice |
| 3-minor-unit currency | `<uint:64,KWD>` | Value in fils |
| Commodity price | `<float_dec:64,USD/oz_t>` | $/troy oz |
| Hourly rate | `<float_dec:64,EUR/h>` | |
| Exchange rate | `<float_dec:64,USD/EUR>` | USD per EUR |
| On-chain satoshi balance | `<uint:64,BTC>` | Integer satoshis |
| On-chain wei balance | `<uint:64,ETH>` | Integer wei |
| Human-readable BTC amount | `<float_dec:64,BTC>` | |

> **`float` (binary floating-point) is discouraged** for monetary amounts.
> Binary fractions cannot represent 0.10 USD exactly (0.1 has no finite binary
> expansion).  Use `float_dec` for decimal-exact storage.
>
> **`float_fix` is wrong** for monetary values.  Q-format stores values as
> `integer × 2^(-N)` — a binary fractional resolution.  No power of 2 equals
> a power of 10 (except 2⁰ = 10⁰ = 1), so `float_fix:64,q7` gives resolution
> 2⁻⁷ ≈ 0.0078, not 0.01.  There is no Q value that exactly represents cents.

### 12.8 New C API

```c
#include "bovnar_currency.h"

bool                       bvn_unit_is_currency(int base);
bool                       bvn_unit_is_fiat    (int base);
bool                       bvn_unit_is_crypto  (int base);
uint8_t                    bvn_currency_minor_unit(int base, bool *ok);
const bvn_currency_info_t *bvn_currency_info   (int base);
int                        bvn_parse_currency_str(const uint8_t *s, uint32_t len);
bool                       bvn_currency_prefix_valid(int base, int prefix_system);
```

```c
bool ok;
uint8_t n = bvn_currency_minor_unit(bu_kwd, &ok);  /* n=3, ok=true  */
uint8_t m = bvn_currency_minor_unit(bu_jpy, &ok);  /* m=0, ok=true  */
uint8_t x = bvn_currency_minor_unit(bu_meter, &ok);/* x=0, ok=false */

const bvn_currency_info_t *ci = bvn_currency_info(bu_usd);
/* ci->code="USD", ci->numeric_code=840, ci->minor_unit=2,
   ci->is_crypto=false, ci->name="US Dollar" */

int cv = bvn_parse_currency_str((const uint8_t *)"EUR", 3);  /* cv=176 */
int cc = bvn_parse_currency_str((const uint8_t *)"DOGE", 4); /* cc=323 */
int cx = bvn_parse_currency_str((const uint8_t *)"xyz", 3);  /* cx=0   */
```

### 12.9 New Python API

```python
from bovnar.enums import BaseUnit
from bovnar.currency import (
    is_currency, is_fiat, is_crypto,
    minor_unit, currency_info, currency_name, from_code,
    all_fiat, all_crypto,
)

assert is_currency(BaseUnit.USD)        # True
assert is_fiat(BaseUnit.XAU)            # True (gold is ISO 4217 X-code)
assert is_crypto(BaseUnit.ETH)          # True
assert not is_fiat(BaseUnit.BTC)        # True

assert minor_unit(BaseUnit.USD)  == 2   # cents
assert minor_unit(BaseUnit.JPY)  == 0   # indivisible
assert minor_unit(BaseUnit.KWD)  == 3   # fils
assert minor_unit(BaseUnit.BTC)  == 8   # satoshis
assert minor_unit(BaseUnit.ETH)  == 18  # wei

info = currency_info(BaseUnit.EUR)
assert info.code == "EUR"
assert info.numeric_code == 978
assert info.minor_unit == 2

btc = from_code("BTC")
assert btc == BaseUnit.BTC

fiat_count  = sum(1 for _ in all_fiat())   # 161
crypto_count = sum(1 for _ in all_crypto()) # 34
```

### 12.10 Validation Rules

One new error condition is added to §12.8 of the main specification:

| Check | Error |
|---|---|
| IEC binary prefix (`Ki`…`Qi`) applied to any currency base unit | `error_unit_illegal` |

All other existing validation rules apply unchanged.  In particular, unknown or malformed currency tokens (e.g. `XYZ`, `usd`, `123`) produce `error_unit_illegal` via the existing "unrecognised base unit" path.

### 12.11 Examples

```bovnar
# ── Fiat scalar amounts ────────────────────────────────────────────────────
.price_usd     = <float_dec:64,USD>   19.99;
.balance_eur   = <float_dec:64,EUR>  342.00;
.yen_fee       = <uint:64,JPY>          500;
.kwd_invoice   = <uint:64,KWD>         3500;  # 3.500 KWD in fils

# ── Crypto scalar amounts ──────────────────────────────────────────────────
.btc_sat       = <uint:64,BTC>   54782000;    # on-chain satoshis
.eth_readable  = <float_dec:64,ETH>    2.5;
.doge_bag      = <float_dec:64,DOGE> 42000.0;
.usdt_stable   = <float_dec:64,USDT>  5000.00;

# ── Compound units ─────────────────────────────────────────────────────────
.gold_price    = <float_dec:64,USD/oz_t>   2351.40;
.btc_price_eur = <float_dec:64,EUR/BTC>   63280.00;
.eth_gwei      = <float_dec:64,G~ETH>       35.0;

# ── Reporting scale ────────────────────────────────────────────────────────
.nav_kusd      = <float_dec:64,k~USD>    250.0;    # $250,000
.tvl_musdt     = <float_dec:64,M~USDT>  1234.5;   # $1.2345B TVL

# ── Array of prices ────────────────────────────────────────────────────────
.tier_prices   = <float_dec:64,USD> [9.99, 19.99, 49.99, 99.99];

# ── Invalid: IEC prefix on currency ───────────────────────────────────────
# .bad = <float_dec:64,Ki~USD> 1;    # → error_unit_illegal
```

### 12.12 Exchange Rate Handling

Bovnar annotates denomination; it does not store exchange rates or timestamps.  An exchange rate annotation declares the *ratio unit* of a value:

```bovnar
.eur_usd = <float_dec:64,USD/EUR> 1.0842;   # at time of snapshot
```

The timestamp belongs in a separate field:

```bovnar
.snapshot = {
    .epoch    = <uint:64,s>    1716400000;
    .eur_usd  = <float_dec:64,USD/EUR> 1.0842;
};
```

Resolution of the rate against a specific timestamp is entirely the application's concern — identical to how Bovnar stores `<float:64,m>` but provides no feet-to-meter conversion table.
