#ifndef BOVNAR_CURRENCY_H
#define BOVNAR_CURRENCY_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BVN_CURRENCY_FIAT_FIRST    134
#define BVN_CURRENCY_FIAT_LAST     294
#define BVN_CURRENCY_CRYPTO_FIRST  295
#define BVN_CURRENCY_CRYPTO_LAST   328
#define BVN_VALUE_BASE_UNIT_COUNT_CURRENCY  329
typedef struct {
    char     code[5];
    uint16_t numeric_code;
    uint8_t  minor_unit;
    bool     is_crypto;
    char     name[48];
} bvn_currency_info_t;
bool bvn_unit_is_currency(int base);
bool bvn_unit_is_fiat(int base);
bool bvn_unit_is_crypto(int base);
uint8_t bvn_currency_minor_unit(int base, bool *ok);
const bvn_currency_info_t *bvn_currency_info(int base);
int bvn_parse_currency_str(const uint8_t *s, uint32_t len);
bool bvn_currency_prefix_valid(int base, int prefix_system);
#define BVN_UNIT_FIAT(e)   BVN_UNIT_NO_PREFIX(e)
#define BVN_UNIT_CRYPTO(e) BVN_UNIT_NO_PREFIX(e)
#ifdef __cplusplus
}
#endif
#endif
