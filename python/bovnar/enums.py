

from enum import IntEnum

class Event(IntEnum):

    STREAM_START                        = 0
    ASSIGNMENT_START                    = 1
    OCTET_STREAM_START                  = 2
    OCTET_STREAM_END                    = 3
    STRUCT_START                        = 4
    STRUCT_END                          = 5
    ARRAY_ROW_START                     = 6
    ARRAY_ROW_END                       = 7
    ARRAY_DIM_START                     = 8
    DATA                                = 9
    TYPE_ANNOTATION_START               = 10
    TYPE_ANNOTATION_END                 = 11
    TYPE_ANNOTATION_TYPE_FAMILY         = 12
    TYPE_ANNOTATION_TYPE_FAMILY_PARAM   = 13

class ValueTypeFamily(IntEnum):

    PLAIN       = 0
    UTF8        = 1
    SINT        = 2
    UINT        = 3
    FLOAT       = 4
    FLOAT_FIX   = 5
    FLOAT_DEC   = 6
    ILLEGAL     = 7

class PrefixSystem(IntEnum):

    SI          = 0
    IEC         = 1
    _SENTINEL   = 2

class SIPrefix(IntEnum):

    NONE    = 0
    QUECTO  = 1
    RONTO   = 2
    YOCTO   = 3
    ZEPTO   = 4
    ATTO    = 5
    FEMTO   = 6
    PICO    = 7
    NANO    = 8
    MICRO   = 9
    MILLI   = 10
    CENTI   = 11
    DECI    = 12
    DECA    = 13
    HECTO   = 14
    KILO    = 15
    MEGA    = 16
    GIGA    = 17
    TERA    = 18
    PETA    = 19
    EXA     = 20
    ZETTA   = 21
    YOTTA   = 22
    RONNA   = 23
    QUETTA  = 24
    _SENTINEL = 25

class IECPrefix(IntEnum):

    NONE    = 0
    KIBI    = 1
    MEBI    = 2
    GIBI    = 3
    TEBI    = 4
    PEBI    = 5
    EXBI    = 6
    ZEBI    = 7
    YOBI    = 8
    ROBI    = 9
    QUEBI   = 10
    _SENTINEL = 11

class BaseUnit(IntEnum):

    NONE                = 0
    BIT                 = 1
    BYTE                = 2
    SECOND              = 3
    METER               = 4
    GRAM                = 5
    AMPERE              = 6
    KELVIN              = 7
    MOL                 = 8
    CANDELA             = 9
    HERTZ               = 10
    NEWTON              = 11
    PASCAL              = 12
    JOULE               = 13
    WATT                = 14
    VOLT                = 15
    OHM                 = 16
    FARAD               = 17
    COULOMB             = 18
    SIEMENS             = 19
    WEBER               = 20
    TESLA               = 21
    HENRY               = 22
    LUMEN               = 23
    LUX                 = 24
    BECQUEREL           = 25
    GRAY                = 26
    SIEVERT             = 27
    KATAL               = 28
    LITER               = 29
    MINUTE              = 30
    HOUR                = 31
    DAY                 = 32
    DEGREE              = 33
    CELSIUS             = 34
    RADIAN              = 35
    STERADIAN           = 36
    TONNE               = 37
    BAR                 = 38
    ELECTRONVOLT        = 39
    DALTON              = 40
    ASTRONOMICAL_UNIT   = 41
    HECTARE             = 42
    WEEK                = 43
    YEAR                = 44
    INCH                = 45
    FOOT                = 46
    YARD                = 47
    MILE                = 48
    NAUTICAL_MILE       = 49
    ANGSTROM            = 50
    LIGHT_YEAR          = 51
    PARSEC              = 52
    FURLONG             = 53
    FATHOM              = 54
    POUND               = 55
    OUNCE               = 56
    GRAIN               = 57
    STONE               = 58
    SHORT_TON           = 59
    LONG_TON            = 60
    TROY_OUNCE          = 61
    CARAT               = 62
    FAHRENHEIT          = 63
    ATMOSPHERE          = 64
    MMHG                = 65
    TORR                = 66
    PSI                 = 67
    CALORIE             = 68
    BTU                 = 69
    ERG                 = 70
    THERM               = 71
    HORSEPOWER          = 72
    POUND_FORCE         = 73
    DYNE                = 74
    KIP                 = 75
    KNOT                = 76
    GALLON              = 77
    GALLON_UK           = 78
    QUART               = 79
    PINT                = 80
    CUP                 = 81
    FLUID_OUNCE         = 82
    TABLESPOON          = 83
    TEASPOON            = 84
    BARREL              = 85
    ACRE                = 86
    BARN                = 87
    ARCMINUTE           = 88
    ARCSECOND           = 89
    GRAD                = 90
    POISE               = 91
    STOKES              = 92
    GAUSS               = 93
    MAXWELL             = 94
    OERSTED             = 95
    STILB               = 96
    PHOT                = 97
    GALILEO             = 98
    CURIE               = 99
    ROENTGEN            = 100
    REM                 = 101
    NEPER               = 102
    _SENTINEL           = 103

class Exponent(IntEnum):

    INVALID     =  0
    LINEAR      =  1
    SQUARE      =  2
    CUBIC       =  3
    QUARTIC     =  4
    QUINTIC     =  5
    SEXTIC      =  6
    SEPTIC      =  7
    OCTIC       =  8
    NONIC       =  9
    NEG_LINEAR  = -1
    NEG_SQUARE  = -2
    NEG_CUBIC   = -3
    NEG_QUARTIC = -4
    NEG_QUINTIC = -5
    NEG_SEXTIC  = -6
    NEG_SEPTIC  = -7
    NEG_OCTIC   = -8
    NEG_NONIC   = -9

class ErrorCode(IntEnum):

    NONE                        = 0
    UNKNOWN_TOKEN_TYPE          = 1
    ARRAY_ROW_SIZE_MISMATCH     = 2
    IDENTIFIER_TOO_LONG         = 3
    EMPTY_IDENTIFIER            = 4
    STRUCT_NESTING_TOO_HIGH     = 5
    ARRAY_NESTING_TOO_HIGH      = 6
    ILLEGAL_STRUCT_CLOSE        = 7
    STRING_TOO_LONG             = 8
    ILLEGAL_ESCAPE_SEQUENCE     = 9
    NUMBER_TOO_LONG             = 10
    SYMBOL_TOO_LONG             = 11
    REFERENCE_TOO_LONG          = 12
    READ_COMPLETE_CHUNK_FAILED  = 13
    OCTET_STREAM_OUT_OF_SYNC    = 14
    UNEXPECTED_INPUT_BYTE       = 15
    TEXT_DATA_TOO_LONG          = 16
    READING_FROM_SOURCE_FD      = 17
    GOT_INCOMPLETE_BVNR_STREAM  = 18
    INVALID_UTF8_BYTE           = 19
    INVALID_BYTE_ORDER_MARK     = 20
    TYPE_TOO_LONG               = 21
    UNIT_TOO_LONG               = 22
    EXPECTED_STRING_IN_ARRAY    = 23
    EXPECTED_NUMBER_IN_ARRAY    = 24
    ILLEGAL_VALUE_TYPE          = 25
    SCANNER_CALLBACK_FAILED     = 26
    FILE_TOO_LONG               = 27
    INVALID_ARGUMENT            = 28
    TOO_MANY_ARRAY_ITEMS        = 29
    WRITING_TO_SINK             = 30
    SINK_BUFFER_EXHAUSTED       = 31
    UNIT_ILLEGAL                = 32
    BASE_REQUIRES_STRING_LITERAL = 33
    TYPE_VALUE_MISMATCH         = 34
    VALUE_OUT_OF_RANGE          = 35
    DIGIT_NOT_IN_BASE           = 36
    RECOVERED                   = 37
    UNIT_MISMATCH               = 38
