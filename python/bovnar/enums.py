# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 Janos Sonntag
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

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
    STREAM_END                          = 14


class TokenType(IntEnum):
    # Mirrors C `token_type_t` (include/bovnar.h). This reaches users as the raw
    # int in BvnrData.type, and the writer path selects behaviour on it, so it
    # belongs here beside the other public enums rather than being re-declared as
    # bare integers at each use site. test_enums.py checks it against C.

    IDENTIFIER    = 0
    STRING        = 1
    NUMBER        = 2
    SYMBOL        = 3
    REFERENCE     = 4
    ARRAY_NUMBER  = 5
    ARRAY_STRING  = 6
    TYPE          = 7
    OCTET_STREAM  = 8
    NULL_VALUE    = 9
    STRUCTURE     = 10
    UNIT          = 11
    TYPE_WIDTH    = 12
    TYPE_BASE     = 13
    TYPE_Q        = 14
    BOOL          = 15
    UNKNOWN       = 16


class ValueTypeFamily(IntEnum):

    PLAIN       = 0
    UTF8        = 1
    SINT        = 2
    UINT        = 3
    FLOAT       = 4
    FLOAT_FIX   = 5
    FLOAT_DEC   = 6
    BOOL        = 7
    DATETIME    = 8
    ILLEGAL     = 9


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

    # ---- Native units: block 10 ------------------------------------------
    #
    # One contiguous run from BaseUnit.BIT, in the order of
    # src/gendata/units.bvnr, which is what the C enum is generated from.
    # The section headings below mark groups within that run; they are
    # descriptive, not structural.

    NONE                = 0
    BIT                 = 100000
    BYTE                = 100001
    SECOND              = 100002
    METER               = 100003
    GRAM                = 100004
    AMPERE              = 100005
    KELVIN              = 100006
    MOL                 = 100007
    CANDELA             = 100008
    HERTZ               = 100009
    NEWTON              = 100010
    PASCAL              = 100011
    JOULE               = 100012
    WATT                = 100013
    VOLT                = 100014
    OHM                 = 100015
    FARAD               = 100016
    COULOMB             = 100017
    SIEMENS             = 100018
    WEBER               = 100019
    TESLA               = 100020
    HENRY               = 100021
    LUMEN               = 100022
    LUX                 = 100023
    BECQUEREL           = 100024
    GRAY                = 100025
    SIEVERT             = 100026
    KATAL               = 100027
    LITER               = 100028
    MINUTE              = 100029
    HOUR                = 100030
    DAY                 = 100031
    DEGREE              = 100032
    CELSIUS             = 100033
    RADIAN              = 100034
    STERADIAN           = 100035
    TONNE               = 100036
    BAR                 = 100037
    ELECTRONVOLT        = 100038
    DALTON              = 100039
    ASTRONOMICAL_UNIT   = 100040
    HECTARE             = 100041
    WEEK                = 100042
    YEAR                = 100043
    INCH                = 100044
    FOOT                = 100045
    YARD                = 100046
    MILE                = 100047
    NAUTICAL_MILE       = 100048
    ANGSTROM            = 100049
    LIGHT_YEAR          = 100050
    PARSEC              = 100051
    FURLONG             = 100052
    FATHOM              = 100053
    POUND               = 100054
    OUNCE               = 100055
    GRAIN               = 100056
    STONE               = 100057
    SHORT_TON           = 100058
    LONG_TON            = 100059
    TROY_OUNCE          = 100060
    CARAT               = 100061
    FAHRENHEIT          = 100062
    ATMOSPHERE          = 100063
    MMHG                = 100064
    TORR                = 100065
    PSI                 = 100066
    CALORIE             = 100067
    BTU                 = 100068
    ERG                 = 100069
    THERM               = 100070
    HORSEPOWER          = 100071
    POUND_FORCE         = 100072
    DYNE                = 100073
    KIP                 = 100074
    KNOT                = 100075
    GALLON              = 100076
    GALLON_UK           = 100077
    QUART               = 100078
    PINT                = 100079
    CUP                 = 100080
    FLUID_OUNCE         = 100081
    TABLESPOON          = 100082
    TEASPOON            = 100083
    BARREL              = 100084
    ACRE                = 100085
    BARN                = 100086
    ARCMINUTE           = 100087
    ARCSECOND           = 100088
    GRAD                = 100089
    POISE               = 100090
    STOKES              = 100091
    GAUSS               = 100092
    MAXWELL             = 100093
    OERSTED             = 100094
    STILB               = 100095
    PHOT                = 100096
    GALILEO             = 100097
    CURIE               = 100098
    ROENTGEN            = 100099
    REM                 = 100100
    NEPER               = 100101
    DECIBEL             = 100102
    RANKINE             = 100103
    SLUG                = 100104
    THOU                = 100105
    PINT_UK             = 100106
    FLUID_OUNCE_UK      = 100107
    QUART_UK            = 100108
    VAR                 = 100109
    VOLT_AMPERE         = 100110
    KILOGRAM_FORCE      = 100111
    INCH_HG             = 100112
    RPM                 = 100113
    FOOT_POUND          = 100114
    DRAM                = 100115
    PENNYWEIGHT         = 100116
    CHAIN               = 100117
    ROD                 = 100118
    GILL                = 100119
    GILL_UK             = 100120
    STANDARD_GRAVITY    = 100121
    METRIC_HORSEPOWER   = 100122
    REVOLUTION          = 100123
    MONTH               = 100124
    FORTNIGHT           = 100125
    ATMOSPHERE_TECHNICAL = 100126
    TEX                 = 100127
    DENIER              = 100128
    FLUID_DRAM          = 100129
    MINIM               = 100130
    PECK                = 100131
    BUSHEL              = 100132

    # Historical German units

    PFUND              = 100133
    ZENTNER            = 100134
    DOPPELZENTNER      = 100135
    LOT                = 100136
    PRUSSIAN_LINE      = 100137
    PRUSSIAN_ZOLL      = 100138
    PRUSSIAN_FUSS      = 100139
    PRUSSIAN_ELLE      = 100140
    PRUSSIAN_RUTE      = 100141
    KLAFTER            = 100142
    GERMAN_MILE        = 100143
    MORGEN             = 100144
    SCHEFFEL           = 100145

    # Further physical units

    SURVEY_FOOT        = 100146
    LEAGUE             = 100147
    CABLE              = 100148
    HAND               = 100149
    QUINTAL            = 100150
    SCRUPLE            = 100151
    BAUD               = 100152

    # Historical temperature scales

    DELISLE            = 100153
    NEWTON_TEMP        = 100154
    REAUMUR            = 100155
    ROMER              = 100156

    # Ratio and proportion units

    PERCENT            = 100157
    PER_MILLE          = 100158
    PER_MYRIAD         = 100159
    PER_CENT_MILLE     = 100160
    PPM                = 100161
    PPB                = 100162


    PH_SCALE           = 100163   # acidity; dimensionless, takes no prefix
    MILE_PER_HOUR      = 100164
    KILOMETER_PER_HOUR = 100165

    # Water hardness: six scales for one quantity, the concentration
    # of dissolved alkaline-earth ions. All carry mol*m^-3, so they convert into
    # each other and into m~mol/L. VAL is the equivalent as water analysis uses
    # it (divalent: 1 val = 0.5 mol).

    GERMAN_HARDNESS    = 100166   # °dH
    ENGLISH_HARDNESS   = 100167   # °e, °Clark
    FRENCH_HARDNESS    = 100168   # °fH
    RUSSIAN_HARDNESS   = 100169   # °rH
    AMERICAN_HARDNESS  = 100170   # °aH (water chemistry's "ppm")
    VAL                = 100171
    GRAINS_PER_GALLON  = 100172   # gpg — the US water-softener hardness scale

    # Water-quality instrument scales. NTU, FNU and PSU are
    # dimensionless and each carries its own quantity kind, so none of them
    # converts to another or to a plain number. CF is a rescaled conductivity
    # and does carry siemens-per-metre dimensions.

    TURBIDITY_NTU      = 100173   # white light, EPA 180.1
    TURBIDITY_FNU      = 100174   # near-infrared, ISO 7027
    PRACTICAL_SALINITY = 100175   # PSS-78; a conductivity ratio, not a mass fraction
    CONDUCTIVITY_FACTOR = 100176  # CF = EC in mS/cm × 10
    TURBIDITY_FTU      = 100177   # formazin, geometry unstated (ISO 7027:1984)
    TURBIDITY_FAU      = 100178   # attenuation at 0°, not scatter
    TURBIDITY_JTU      = 100179   # Jackson candle method; no formazin relation

    # Temperature INTERVALS. A rise of 25 degrees is 25 K; 25 °C is 298.15 K.
    # These carry the same dimension as the scales and a quantity kind of their
    # own, so ΔK does not convert into K (see units.bvnr). Δ°C shares
    # DELTA_KELVIN and Δ°Ra shares DELTA_FAHRENHEIT: the Celsius degree IS the
    # kelvin and the Rankine degree IS the Fahrenheit degree, so those are
    # aliases rather than rows.
    DELTA_KELVIN       = 100180   # ΔK, Δ°C
    DELTA_FAHRENHEIT   = 100181   # Δ°F, Δ°Ra
    DELTA_DELISLE      = 100182
    DELTA_NEWTON_TEMP  = 100183
    DELTA_REAUMUR      = 100184
    DELTA_ROMER        = 100185

    # Units the unit profiles needed and this registry did not have; each was
    # the sole reason a run of UCUM, UDUNITS, QUDT and UN/ECE codes had to be
    # refused. See the note beside them in src/gendata/units.bvnr.
    METER_WATER        = 100186   # mH2O, the conventional 9806.65 Pa column
    CALORIE_IT         = 100187   # 4.1868 J; native CALORIE is thermochemical
    BTU_TH             = 100188   # thermochemical BTU; native BTU is the IT one
    TROY_POUND         = 100189   # 12 troy ounces; also the apothecary pound
    APOTHECARY_DRAM    = 100190   # 3 scruples, not the avoirdupois DRAM
    LONG_HUNDREDWEIGHT = 100191   # 112 lb; the short one is exactly h~lb

    # The survey lengths, the typographic lengths, the US dry volumes and the
    # singles that blocked whole families of publisher codes. See the notes
    # beside them in src/gendata/units.bvnr.
    # ---- US survey: exact rationals on the 1200/3937 m survey foot
    SURVEY_INCH        = 100192   # inUS
    SURVEY_YARD        = 100193   # ydUS
    SURVEY_FATHOM      = 100194   # fathUS
    SURVEY_ROD         = 100195   # rdUS
    SURVEY_CHAIN       = 100196   # chUS
    SURVEY_LINK        = 100197   # lkUS
    SURVEY_FURLONG     = 100198   # furUS
    SURVEY_MILE        = 100199   # miUS
    SURVEY_ACRE        = 100200   # acUS
    # ---- typographic: the DTP point and what is built on it
    POINT              = 100201   # pnt
    PICA               = 100202   # pca
    LINE               = 100203   # lne
    # ---- the US DRY volumes, 16 % larger than the liquid ones
    DRY_GALLON         = 100204   # gal_dry
    DRY_QUART          = 100205   # qt_dry
    DRY_PINT           = 100206   # pt_dry
    # ---- and the singles
    BOARD_FOOT         = 100207   # fbm
    CORD               = 100208   # cord
    SURVEY_ACRE_FOOT   = 100209   # ac_ft
    DARCY              = 100210   # darcy
    THERM_EC           = 100211   # thm_ec
    REFRIGERATION_TON  = 100212   # ton_ref
    DOBSON             = 100213   # DU
    SHAKE              = 100214   # shake
    # The two ratios below ppb. The SYMBOL is `pptr`, not `ppt`: `ppt`
    # already resolves as p~pt, the picopint, and units.bvnr forbids a new
    # alias that changes an existing spelling. UCUM spells it [pptr] too.
    PPT                = 100215   # pptr
    PPQ                = 100216   # ppq

    # ---- Currencies: block 90 -------------------------------------------
    #
    # One contiguous run, fiat then crypto, mirroring src/gendata/
    # currencies.bvnr. minor_unit is the decimal places the currency is
    # quoted in; for crypto it is the on-chain canonical scale and the ISO
    # numeric code is 0. Whether a currency is crypto is a property of the
    # catalogue row (bovnar.currency), never of where its id falls.
    #
    # Unlike the units above, these names are a CONVENIENCE: a document
    # writes a currency by its code behind a '$' sigil, and the C library
    # gives them no enumerators at all.

    AED = 900000   # UAE Dirham              - minor unit: 2
    AFN = 900001   # Afghan Afghani          - minor unit: 2
    ALL = 900002   # Albanian Lek            - minor unit: 2
    AMD = 900003   # Armenian Dram           - minor unit: 2
    ANG = 900004   # NL Antillean Guilder    - minor unit: 2
    AOA = 900005   # Angolan Kwanza          - minor unit: 2
    ARS = 900006   # Argentine Peso          - minor unit: 2
    AUD = 900007   # Australian Dollar       - minor unit: 2
    AWG = 900008   # Aruban Florin           - minor unit: 2
    AZN = 900009   # Azerbaijani Manat       - minor unit: 2
    BAM = 900010   # BH Convertible Mark     - minor unit: 2
    BBD = 900011   # Barbados Dollar         - minor unit: 2
    BDT = 900012   # Bangladeshi Taka        - minor unit: 2
    BGN = 900013   # Bulgarian Lev           - minor unit: 2
    BHD = 900014   # Bahraini Dinar          - minor unit: 3
    BIF = 900015   # Burundian Franc         - minor unit: 0
    BMD = 900016   # Bermudian Dollar        - minor unit: 2
    BND = 900017   # Brunei Dollar           - minor unit: 2
    BOB = 900018   # Boliviano               - minor unit: 2
    BRL = 900019   # Brazilian Real          - minor unit: 2
    BSD = 900020   # Bahamian Dollar         - minor unit: 2
    BTN = 900021   # Bhutanese Ngultrum      - minor unit: 2
    BWP = 900022   # Botswana Pula           - minor unit: 2
    BYN = 900023   # Belarusian Ruble        - minor unit: 2
    BZD = 900024   # Belize Dollar           - minor unit: 2
    CAD = 900025   # Canadian Dollar         - minor unit: 2
    CDF = 900026   # Congolese Franc         - minor unit: 2
    CHF = 900027   # Swiss Franc             - minor unit: 2
    CLF = 900028   # Unidad de Fomento       - minor unit: 4
    CLP = 900029   # Chilean Peso            - minor unit: 0
    CNY = 900030   # Chinese Yuan            - minor unit: 2
    COP = 900031   # Colombian Peso          - minor unit: 2
    CRC = 900032   # Costa Rican Colon       - minor unit: 2
    CUP_ = 900033  # Cuban Peso  - minor unit: 2  (trailing _ : the bare name CUP is the US-cup volume unit = 100080; wire token is still bare uppercase "CUP")
    CVE = 900034   # Cape Verdean Escudo     - minor unit: 2
    CZK = 900035   # Czech Koruna            - minor unit: 2
    DJF = 900036   # Djiboutian Franc        - minor unit: 0
    DKK = 900037   # Danish Krone            - minor unit: 2
    DOP = 900038   # Dominican Peso          - minor unit: 2
    DZD = 900039   # Algerian Dinar          - minor unit: 2
    EGP = 900040   # Egyptian Pound          - minor unit: 2
    ERN = 900041   # Eritrean Nakfa          - minor unit: 2
    ETB = 900042   # Ethiopian Birr          - minor unit: 2
    EUR = 900043   # Euro                    - minor unit: 2
    FJD = 900044   # Fijian Dollar           - minor unit: 2
    FKP = 900045   # Falkland Islands Pound  - minor unit: 2
    GBP = 900046   # Pound Sterling          - minor unit: 2
    GEL = 900047   # Georgian Lari           - minor unit: 2
    GHS = 900048   # Ghanaian Cedi           - minor unit: 2
    GIP = 900049   # Gibraltar Pound         - minor unit: 2
    GMD = 900050   # Gambian Dalasi          - minor unit: 2
    GNF = 900051   # Guinean Franc           - minor unit: 0
    GTQ = 900052   # Guatemalan Quetzal      - minor unit: 2
    GYD = 900053   # Guyanese Dollar         - minor unit: 2
    HKD = 900054   # Hong Kong Dollar        - minor unit: 2
    HNL = 900055   # Honduran Lempira        - minor unit: 2
    HRK = 900056   # Croatian Kuna           - minor unit: 2
    HTG = 900057   # Haitian Gourde          - minor unit: 2
    HUF = 900058   # Hungarian Forint        - minor unit: 2
    IDR = 900059   # Indonesian Rupiah       - minor unit: 2
    ILS = 900060   # Israeli New Shekel      - minor unit: 2
    INR = 900061   # Indian Rupee            - minor unit: 2
    IQD = 900062   # Iraqi Dinar             - minor unit: 3
    IRR = 900063   # Iranian Rial            - minor unit: 2
    ISK = 900064   # Icelandic Krona         - minor unit: 0
    JMD = 900065   # Jamaican Dollar         - minor unit: 2
    JOD = 900066   # Jordanian Dinar         - minor unit: 3
    JPY = 900067   # Japanese Yen            - minor unit: 0
    KES = 900068   # Kenyan Shilling         - minor unit: 2
    KGS = 900069   # Kyrgyzstani Som         - minor unit: 2
    KHR = 900070   # Cambodian Riel          - minor unit: 2
    KMF = 900071   # Comorian Franc          - minor unit: 0
    KPW = 900072   # North Korean Won        - minor unit: 2
    KRW = 900073   # South Korean Won        - minor unit: 0
    KWD = 900074   # Kuwaiti Dinar           - minor unit: 3
    KYD = 900075   # Cayman Islands Dollar   - minor unit: 2
    KZT = 900076   # Kazakhstani Tenge       - minor unit: 2
    LAK = 900077   # Laotian Kip             - minor unit: 2
    LBP = 900078   # Lebanese Pound          - minor unit: 2
    LKR = 900079   # Sri Lankan Rupee        - minor unit: 2
    LRD = 900080   # Liberian Dollar         - minor unit: 2
    LSL = 900081   # Lesotho Loti            - minor unit: 2
    LYD = 900082   # Libyan Dinar            - minor unit: 3
    MAD = 900083   # Moroccan Dirham         - minor unit: 2
    MDL = 900084   # Moldovan Leu            - minor unit: 2
    MGA = 900085   # Malagasy Ariary         - minor unit: 2
    MKD = 900086   # Macedonian Denar        - minor unit: 2
    MMK = 900087   # Myanmar Kyat            - minor unit: 2
    MNT = 900088   # Mongolian Togrog        - minor unit: 2
    MOP = 900089   # Macanese Pataca         - minor unit: 2
    MRU = 900090   # Mauritanian Ouguiya     - minor unit: 2
    MUR = 900091   # Mauritian Rupee         - minor unit: 2
    MVR = 900092   # Maldivian Rufiyaa       - minor unit: 2
    MWK = 900093   # Malawian Kwacha         - minor unit: 2
    MXN = 900094   # Mexican Peso            - minor unit: 2
    MYR = 900095   # Malaysian Ringgit       - minor unit: 2
    MZN = 900096   # Mozambican Metical      - minor unit: 2
    NAD = 900097   # Namibian Dollar         - minor unit: 2
    NGN = 900098   # Nigerian Naira          - minor unit: 2
    NIO = 900099   # Nicaraguan Cordoba      - minor unit: 2
    NOK = 900100   # Norwegian Krone         - minor unit: 2
    NPR = 900101   # Nepalese Rupee          - minor unit: 2
    NZD = 900102   # New Zealand Dollar      - minor unit: 2
    OMR = 900103   # Omani Rial              - minor unit: 3
    PAB = 900104   # Panamanian Balboa       - minor unit: 2
    PEN = 900105   # Peruvian Sol            - minor unit: 2
    PGK = 900106   # Papua New Guinean Kina  - minor unit: 2
    PHP = 900107   # Philippine Peso         - minor unit: 2
    PKR = 900108   # Pakistani Rupee         - minor unit: 2
    PLN = 900109   # Polish Zloty            - minor unit: 2
    PYG = 900110   # Paraguayan Guarani      - minor unit: 0
    QAR = 900111   # Qatari Riyal            - minor unit: 2
    RON = 900112   # Romanian Leu            - minor unit: 2
    RSD = 900113   # Serbian Dinar           - minor unit: 2
    RUB = 900114   # Russian Ruble           - minor unit: 2
    RWF = 900115   # Rwandan Franc           - minor unit: 0
    SAR = 900116   # Saudi Riyal             - minor unit: 2
    SBD = 900117   # Solomon Islands Dollar  - minor unit: 2
    SCR = 900118   # Seychellois Rupee       - minor unit: 2
    SDG = 900119   # Sudanese Pound          - minor unit: 2
    SEK = 900120   # Swedish Krona           - minor unit: 2
    SGD = 900121   # Singapore Dollar        - minor unit: 2
    SHP = 900122   # Saint Helena Pound      - minor unit: 2
    SLE = 900123   # Sierra Leonean Leone    - minor unit: 2
    SLL = 900124   # Sierra Leonean Leone (old) - minor unit: 2
    SOS = 900125   # Somali Shilling         - minor unit: 2
    SRD = 900126   # Surinamese Dollar       - minor unit: 2
    SSP = 900127   # South Sudanese Pound    - minor unit: 2
    STN = 900128   # Sao Tome Dobra          - minor unit: 2
    SVC = 900129   # Salvadoran Colon        - minor unit: 2
    SYP = 900130   # Syrian Pound            - minor unit: 2
    SZL = 900131   # Swazi Lilangeni         - minor unit: 2
    THB = 900132   # Thai Baht               - minor unit: 2
    TJS = 900133   # Tajikistani Somoni      - minor unit: 2
    TMT = 900134   # Turkmenistan Manat      - minor unit: 2
    TND = 900135   # Tunisian Dinar          - minor unit: 3
    TOP = 900136   # Tongan Pa'anga          - minor unit: 2
    TRY = 900137   # Turkish Lira            - minor unit: 2
    TTD = 900138   # Trinidad/Tobago Dollar  - minor unit: 2
    TWD = 900139   # New Taiwan Dollar       - minor unit: 2
    TZS = 900140   # Tanzanian Shilling      - minor unit: 2
    UAH = 900141   # Ukrainian Hryvnia       - minor unit: 2
    UGX = 900142   # Ugandan Shilling        - minor unit: 0
    USD = 900143   # US Dollar               - minor unit: 2
    UYU = 900144   # Uruguayan Peso          - minor unit: 2
    UZS = 900145   # Uzbekistani Som         - minor unit: 2
    VES = 900146   # Venezuelan Bolivar      - minor unit: 2
    VND = 900147   # Vietnamese Dong         - minor unit: 0
    VUV = 900148   # Vanuatu Vatu            - minor unit: 0
    WST = 900149   # Samoan Tala             - minor unit: 2
    XAF = 900150   # CFA Franc BEAC          - minor unit: 0
    XAG = 900151   # Silver                  - minor unit: 0
    XAU = 900152   # Gold                    - minor unit: 0
    XCD = 900153   # East Caribbean Dollar   - minor unit: 2
    XCG  = 900154  # Caribbean Guilder   - minor unit: 2
    XDR = 900155   # Special Drawing Rights  - minor unit: 0
    XOF = 900156   # CFA Franc BCEAO         - minor unit: 0
    XPD = 900157   # Palladium               - minor unit: 0
    XPF = 900158   # CFP Franc               - minor unit: 0
    XPT = 900159   # Platinum                - minor unit: 0
    XTS = 900160   # Test                    - minor unit: 0
    YER = 900161   # Yemeni Rial             - minor unit: 2
    ZAR = 900162   # South African Rand      - minor unit: 2
    ZMW = 900163   # Zambian Kwacha          - minor unit: 2
    ZWG  = 900164  # Zimbabwe Gold       - minor unit: 2
    ZWL = 900165   # Zimbabwean Dollar       - minor unit: 2
    BTC  = 900166  # Bitcoin             - minor unit: 8  (satoshi)
    ETH  = 900167  # Ethereum            - minor unit: 18 (wei)
    SOL  = 900168  # Solana              - minor unit: 9  (lamport)
    XRP  = 900169  # XRP                 - minor unit: 6  (drop)
    BNB  = 900170  # BNB                 - minor unit: 18
    ADA  = 900171  # Cardano             - minor unit: 6  (lovelace)
    LTC  = 900172  # Litecoin            - minor unit: 8  (litoshi)
    DOT  = 900173  # Polkadot            - minor unit: 10 (planck)
    XMR  = 900174  # Monero              - minor unit: 12 (piconero)
    ETC  = 900175  # Ethereum Classic    - minor unit: 18
    BCH  = 900176  # Bitcoin Cash        - minor unit: 8
    XLM  = 900177  # Stellar             - minor unit: 7  (stroop)
    FIL  = 900178  # Filecoin            - minor unit: 18
    ICP  = 900179  # Internet Computer   - minor unit: 8
    TRX  = 900180  # TRON                - minor unit: 6
    EOS  = 900181  # EOS                 - minor unit: 4
    VET  = 900182  # VeChain             - minor unit: 18
    NEO  = 900183  # Neo                 - minor unit: 0
    ZEC  = 900184  # Zcash               - minor unit: 8
    UNI  = 900185  # Uniswap             - minor unit: 18
    ARB  = 900186  # Arbitrum            - minor unit: 18
    SUI  = 900187  # Sui                 - minor unit: 9
    TON  = 900188  # Toncoin             - minor unit: 9
    INJ  = 900189  # Injective           - minor unit: 18
    SEI  = 900190  # Sei                 - minor unit: 6
    APT  = 900191  # Aptos               - minor unit: 8
    TAO  = 900192  # Bittensor           - minor unit: 9
    WIF  = 900193  # dogwifhat           - minor unit: 6
    DOGE = 900194  # Dogecoin            - minor unit: 8  (koinu)
    LINK = 900195  # Chainlink           - minor unit: 18
    USDT = 900196  # Tether              - minor unit: 6
    USDC = 900197  # USD Coin            - minor unit: 6
    AVAX = 900198  # Avalanche           - minor unit: 18
    ATOM = 900199  # Cosmos              - minor unit: 6
    POL  = 900200  # Polygon             - minor unit: 18
    NEAR = 900201  # NEAR Protocol       - minor unit: 24
    ALGO = 900202  # Algorand            - minor unit: 6
    HBAR = 900203  # Hedera              - minor unit: 8
    AAVE = 900204  # Aave                - minor unit: 18
    MKR  = 900205  # Maker               - minor unit: 18
    DAI  = 900206  # Dai                 - minor unit: 18
    STX  = 900207  # Stacks              - minor unit: 6
    GRT  = 900208  # The Graph           - minor unit: 18
    LDO  = 900209  # Lido DAO            - minor unit: 18
    BONK = 900210  # Bonk                - minor unit: 5
    PEPE = 900211  # Pepe                - minor unit: 18
    SHIB = 900212  # Shiba Inu           - minor unit: 18
    JUP  = 900213  # Jupiter             - minor unit: 6
    PYTH = 900214  # Pyth Network        - minor unit: 6
    RUNE = 900215  # THORChain           - minor unit: 8


# The blocks of the base-unit id space, mirroring the C #defines. An id's
# leading two digits name the vocabulary it comes from and the four after them
# its position in that vocabulary, so each block holds 10000 ids and growing one
# vocabulary shifts no other. There is no "count" or sentinel: the space is
# sparse by design, and `<= UNIT_BLOCK_SIZE` is not a membership test — ask
# bovnar.currency.is_currency, or compare against a block's own bounds.

UNIT_BLOCK_SIZE     = 10000

UNIT_NATIVE_FIRST   = BaseUnit.BIT
UNIT_NATIVE_LAST    = BaseUnit.PPQ

CURRENCY_FIRST      = BaseUnit.AED
CURRENCY_LAST       = BaseUnit.RUNE
CURRENCY_COUNT      = 216


EXPONENT_MIN = -100
EXPONENT_MAX =  100


class Exponent(IntEnum):
    """A component's exponent: any integer in [EXPONENT_MIN, EXPONENT_MAX].

    The members below name +-1..+-9 because that is what most units need and
    what existing code says; they are NOT the whole domain. Mirroring
    unit_exponent_t in include/bovnar.h, any value in the range is legal, so
    this enum is deliberately OPEN -- Exponent(42) yields a pseudo-member
    rather than raising, because a strict IntEnum here would turn a perfectly
    valid unit read off the wire into a ValueError inside the accessor.

    Zero stays INVALID: a component with exponent 0 is malformed, not
    dimensionless.
    """

    @classmethod
    def _missing_(cls, value):
        if isinstance(value, int) and EXPONENT_MIN <= value <= EXPONENT_MAX:
            obj = int.__new__(cls, value)
            obj._name_ = ("NEG_%d" % -value) if value < 0 else ("POW_%d" % value)
            obj._value_ = value
            return obj
        return None

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
    ARRAY_ELEMENT_TYPE_MISMATCH = 39
    STRUCT_SHAPE_MISMATCH       = 40
    DUPLICATE_STRUCT_KEY        = 41
    INVALID_SPEC_VERSION        = 42
    UNSUPPORTED_SPEC_VERSION    = 43
    INVALID_CODEPOINT           = 44
    INVALID_DATETIME_LITERAL    = 45
    DATETIME_LITERAL_UNSUPPORTED_EPOCH = 46
    UNIT_INEXACT                = 47
    OCTET_STREAM_TRUNCATED      = 48
    # spec 1.2 -- the UCUM unit profile. Both are raised by the validator for a
    # "name:" unit: the first when no such profile exists, the second when the
    # expression is valid in the profile but has no representation here.
    UNIT_PROFILE_UNKNOWN        = 49
    UNIT_PROFILE_UNSUPPORTED    = 50
    # spec 1.2 -- the document contains an octet stream and the reader was
    # opened with text_only. An assertion by the CONSUMER, not a defect in the
    # document.
    OCTET_STREAM_FORBIDDEN      = 51
    # Whitespace split a type-annotation parameter in two. The grammar puts
    # every `ws` inside an annotation beside a separator; in the middle of a
    # parameter it has no production, and the lexer used to drop it anyway --
    # which is how "<float:64,k g>" became a kilogram and the UDUNITS spelling
    # "udunits:m s-1" became a reciprocal millisecond.
    TYPE_PARAM_WHITESPACE       = 52
