#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_internal_dims.h"
#include "bovnar_si_units.h"
#include "bovnar_currency.h"
#include "bvn_unit_impl.h"
int32_t bvn_exponent_to_int(unit_exponent_t e)
{
	switch (e) {
	case exp_invalid:     return 0;
	case exp_linear:      return 1;
	case exp_square:      return 2;
	case exp_cubic:       return 3;
	case exp_quartic:     return 4;
	case exp_quintic:     return 5;
	case exp_sextic:      return 6;
	case exp_septic:      return 7;
	case exp_octic:       return 8;
	case exp_nonic:       return 9;
	case exp_neg_square:  return -2;
	case exp_neg_cubic:   return -3;
	case exp_neg_quartic: return -4;
	case exp_neg_quintic: return -5;
	case exp_neg_sextic:  return -6;
	case exp_neg_septic:  return -7;
	case exp_neg_octic:   return -8;
	case exp_neg_nonic:   return -9;
	case exp_neg_linear:  return -1;
	default:              return 0;
	}
}
unit_exponent_t bvn_int_to_exponent(int32_t n)
{
	switch (n) {
	case  0: return exp_invalid;
	case  1: return exp_linear;
	case  2: return exp_square;
	case  3: return exp_cubic;
	case  4: return exp_quartic;
	case  5: return exp_quintic;
	case  6: return exp_sextic;
	case  7: return exp_septic;
	case  8: return exp_octic;
	case  9: return exp_nonic;
	case -1: return exp_neg_linear;
	case -2: return exp_neg_square;
	case -3: return exp_neg_cubic;
	case -4: return exp_neg_quartic;
	case -5: return exp_neg_quintic;
	case -6: return exp_neg_sextic;
	case -7: return exp_neg_septic;
	case -8: return exp_neg_octic;
	case -9: return exp_neg_nonic;
	default: return exp_invalid;
	}
}
typedef struct {
	value_base_unit_t base;
	double            to_si_factor;
	int32_t           dims[bvn_si_dim_count];
	bool              is_affine;
	double            affine_offset;
} bvn_si_conv_entry_t;
static const bvn_si_conv_entry_t si_conv_table[BVN_VALUE_BASE_UNIT_COUNT] = {
	[bu_none]               = { bu_none,               1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_bit]                = { bu_bit,                1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_byte]               = { bu_byte,               1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_second]             = { bu_second,             1.0,        {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_meter]              = { bu_meter,              1.0,        {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_gram]               = { bu_gram,               1e-3,       {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_ampere]             = { bu_ampere,             1.0,        {0, 0, 0, 1, 0, 0, 0}, false, 0.0    },
	[bu_kelvin]             = { bu_kelvin,             1.0,        {0, 0, 0, 0, 1, 0, 0}, false, 0.0    },
	[bu_mol]                = { bu_mol,                1.0,        {0, 0, 0, 0, 0, 1, 0}, false, 0.0    },
	[bu_candela]            = { bu_candela,            1.0,        {0, 0, 0, 0, 0, 0, 1}, false, 0.0    },
	[bu_hertz]              = { bu_hertz,              1.0,        {0, 0,-1, 0, 0, 0, 0}, false, 0.0    },
	[bu_newton]             = { bu_newton,             1.0,        {1, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_pascal]             = { bu_pascal,             1.0,        {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_joule]              = { bu_joule,              1.0,        {2, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_watt]               = { bu_watt,               1.0,        {2, 1,-3, 0, 0, 0, 0}, false, 0.0    },
	[bu_volt]               = { bu_volt,               1.0,        {2, 1,-3,-1, 0, 0, 0}, false, 0.0    },
	[bu_ohm]                = { bu_ohm,                1.0,        {2, 1,-3,-2, 0, 0, 0}, false, 0.0    },
	[bu_farad]              = { bu_farad,              1.0,        {-2,-1, 4, 2, 0, 0, 0}, false, 0.0   },
	[bu_coulomb]            = { bu_coulomb,            1.0,        {0, 0, 1, 1, 0, 0, 0}, false, 0.0    },
	[bu_siemens]            = { bu_siemens,            1.0,        {-2,-1, 3, 2, 0, 0, 0}, false, 0.0   },
	[bu_weber]              = { bu_weber,              1.0,        {2, 1,-2,-1, 0, 0, 0}, false, 0.0    },
	[bu_tesla]              = { bu_tesla,              1.0,        {0, 1,-2,-1, 0, 0, 0}, false, 0.0    },
	[bu_henry]              = { bu_henry,              1.0,        {2, 1,-2,-2, 0, 0, 0}, false, 0.0    },
	[bu_lumen]              = { bu_lumen,              1.0,        {0, 0, 0, 0, 0, 0, 1}, false, 0.0    },
	[bu_lux]                = { bu_lux,                1.0,        {-2, 0, 0, 0, 0, 0, 1}, false, 0.0   },
	[bu_becquerel]          = { bu_becquerel,          1.0,        {0, 0,-1, 0, 0, 0, 0}, false, 0.0    },
	[bu_gray]               = { bu_gray,               1.0,        {2, 0,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_sievert]            = { bu_sievert,            1.0,        {2, 0,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_katal]              = { bu_katal,              1.0,        {0, 0,-1, 0, 0, 1, 0}, false, 0.0    },
	[bu_liter]              = { bu_liter,              1e-3,       {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_minute]             = { bu_minute,             60.0,       {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_hour]               = { bu_hour,               3600.0,     {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_day]                = { bu_day,                86400.0,    {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_degree]             = { bu_degree,             3.14159265358979323846/180.0,
	                                                               {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_celsius]            = { bu_celsius,            1.0,        {0, 0, 0, 0, 1, 0, 0}, true,  273.15 },
	[bu_radian]             = { bu_radian,             1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_steradian]          = { bu_steradian,          1.0,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_tonne]              = { bu_tonne,              1e3,        {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_bar]                = { bu_bar,                1e5,        {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_electronvolt]       = { bu_electronvolt,       1.602176634e-19, {2, 1,-2, 0, 0, 0, 0}, false, 0.0 },
	[bu_dalton]             = { bu_dalton,             1.66053906660e-27, {0, 1, 0, 0, 0, 0, 0}, false, 0.0 },
	[bu_astronomical_unit]  = { bu_astronomical_unit,  1.495978707e11, {1, 0, 0, 0, 0, 0, 0}, false, 0.0 },
	[bu_hectare]            = { bu_hectare,            1e4,        {2, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_week]               = { bu_week,               604800.0,   {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_year]               = { bu_year,               31557600.0, {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_inch]               = { bu_inch,               0.0254,                   {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_foot]               = { bu_foot,               0.3048,                   {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_yard]               = { bu_yard,               0.9144,                   {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_mile]               = { bu_mile,               1609.344,                 {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_nautical_mile]      = { bu_nautical_mile,      1852.0,                   {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_angstrom]           = { bu_angstrom,           1e-10,                    {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_light_year]         = { bu_light_year,         9.4607304725808e15,       {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_parsec]             = { bu_parsec,             3.085677581491367e16,     {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_furlong]            = { bu_furlong,            201.168,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_fathom]             = { bu_fathom,             1.8288,                   {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_pound]              = { bu_pound,              0.45359237,               {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_ounce]              = { bu_ounce,              0.028349523125,           {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_grain]              = { bu_grain,              6.479891e-5,              {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_stone]              = { bu_stone,              6.35029318,               {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_short_ton]          = { bu_short_ton,          907.18474,                {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_long_ton]           = { bu_long_ton,           1016.0469088,             {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_troy_ounce]         = { bu_troy_ounce,         0.0311034768,             {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_carat]              = { bu_carat,              2e-4,                     {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_fahrenheit]         = { bu_fahrenheit,         5.0/9.0,                  {0, 0, 0, 0, 1, 0, 0}, true,  459.67*(5.0/9.0) },
	[bu_atmosphere]         = { bu_atmosphere,         101325.0,                 {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_mmhg]               = { bu_mmhg,               133.322387415,            {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_torr]               = { bu_torr,               101325.0/760.0,           {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_psi]                = { bu_psi,                6894.757293168361,        {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_calorie]            = { bu_calorie,            4.184,                    {2, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_btu]                = { bu_btu,                1055.05585262,            {2, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_erg]                = { bu_erg,                1e-7,                     {2, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_therm]              = { bu_therm,              1.05480400e8,             {2, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_horsepower]         = { bu_horsepower,         745.69987158227,          {2, 1,-3, 0, 0, 0, 0}, false, 0.0    },
	[bu_pound_force]        = { bu_pound_force,        4.4482216152605,          {1, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_dyne]               = { bu_dyne,               1e-5,                     {1, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_kip]                = { bu_kip,                4448.2216152605,          {1, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_knot]               = { bu_knot,               1852.0/3600.0,            {1, 0,-1, 0, 0, 0, 0}, false, 0.0    },
	[bu_gallon]             = { bu_gallon,             3.785411784e-3,           {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_gallon_uk]          = { bu_gallon_uk,          4.54609e-3,               {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_quart]              = { bu_quart,              9.46352946e-4,            {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_pint]               = { bu_pint,               4.73176473e-4,            {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_cup]                = { bu_cup,                2.365882365e-4,           {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_fluid_ounce]        = { bu_fluid_ounce,        2.95735295625e-5,         {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_tablespoon]         = { bu_tablespoon,         1.478676478125e-5,        {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_teaspoon]           = { bu_teaspoon,           4.92892159375e-6,         {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_barrel]             = { bu_barrel,             0.158987294928,           {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_acre]               = { bu_acre,               4046.8564224,             {2, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_barn]               = { bu_barn,               1e-28,                    {2, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_arcminute]          = { bu_arcminute,          3.14159265358979323846/10800.0,  {0, 0, 0, 0, 0, 0, 0}, false, 0.0 },
	[bu_arcsecond]          = { bu_arcsecond,          3.14159265358979323846/648000.0, {0, 0, 0, 0, 0, 0, 0}, false, 0.0 },
	[bu_grad]               = { bu_grad,               3.14159265358979323846/200.0,    {0, 0, 0, 0, 0, 0, 0}, false, 0.0 },
	[bu_poise]              = { bu_poise,              0.1,                      {-1, 1,-1, 0, 0, 0, 0}, false, 0.0   },
	[bu_stokes]             = { bu_stokes,             1e-4,                     {2, 0,-1, 0, 0, 0, 0}, false, 0.0    },
	[bu_gauss]              = { bu_gauss,              1e-4,                     {0, 1,-2,-1, 0, 0, 0}, false, 0.0    },
	[bu_maxwell]            = { bu_maxwell,            1e-8,                     {2, 1,-2,-1, 0, 0, 0}, false, 0.0    },
	[bu_oersted]            = { bu_oersted,            1000.0/(4.0*3.14159265358979323846), {-1, 0, 0, 1, 0, 0, 0}, false, 0.0 },
	[bu_stilb]              = { bu_stilb,              1e4,                      {-2, 0, 0, 0, 0, 0, 1}, false, 0.0   },
	[bu_phot]               = { bu_phot,               1e4,                      {-2, 0, 0, 0, 0, 0, 1}, false, 0.0   },
	[bu_galileo]            = { bu_galileo,            1e-2,                     {1, 0,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_curie]              = { bu_curie,              3.7e10,                   {0, 0,-1, 0, 0, 0, 0}, false, 0.0    },
	[bu_roentgen]           = { bu_roentgen,           2.58e-4,                  {0,-1, 1, 1, 0, 0, 0}, false, 0.0    },
	[bu_rem]                = { bu_rem,                1e-2,                     {2, 0,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_neper]              = { bu_neper,              1.0,                      {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_decibel]            = { bu_decibel,            1.0,                      {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_rankine]            = { bu_rankine,            5.0/9.0,                  {0, 0, 0, 0, 1, 0, 0}, false, 0.0    },
	[bu_slug]               = { bu_slug,               14.593902937,             {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_thou]               = { bu_thou,               2.54e-5,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_pint_uk]            = { bu_pint_uk,            5.6826125e-4,             {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_fluid_ounce_uk]     = { bu_fluid_ounce_uk,     2.84130625e-5,            {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_quart_uk]           = { bu_quart_uk,           1.1365225e-3,             {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_var]                = { bu_var,                1.0,                      {2, 1,-3, 0, 0, 0, 0}, false, 0.0    },
	[bu_volt_ampere]        = { bu_volt_ampere,        1.0,                      {2, 1,-3, 0, 0, 0, 0}, false, 0.0    },
	[bu_kilogram_force]     = { bu_kilogram_force,     9.80665,                  {1, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_inch_hg]            = { bu_inch_hg,            3386.388645,              {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_rpm]                = { bu_rpm,                1.0/60.0,                 {0, 0,-1, 0, 0, 0, 0}, false, 0.0    },
	[bu_foot_pound]         = { bu_foot_pound,         1.3558179483,             {2, 1,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_dram]               = { bu_dram,               1.7718451953125e-3,       {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_pennyweight]        = { bu_pennyweight,        1.55517384e-3,            {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_chain]              = { bu_chain,              20.1168,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_rod]                = { bu_rod,                5.0292,                   {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_gill]               = { bu_gill,               1.18294118250e-4,         {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_gill_uk]            = { bu_gill_uk,            1.420653125e-4,           {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_standard_gravity]   = { bu_standard_gravity,   9.80665,                  {1, 0,-2, 0, 0, 0, 0}, false, 0.0    },
	[bu_metric_horsepower]  = { bu_metric_horsepower,  735.49875,                {2, 1,-3, 0, 0, 0, 0}, false, 0.0    },
	[bu_revolution]         = { bu_revolution,         6.283185307179586,        {0, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_month]              = { bu_month,              2629800.0,                {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_fortnight]          = { bu_fortnight,          1209600.0,                {0, 0, 1, 0, 0, 0, 0}, false, 0.0    },
	[bu_atmosphere_technical] = { bu_atmosphere_technical, 98066.5,              {-1, 1,-2, 0, 0, 0, 0}, false, 0.0   },
	[bu_tex]                = { bu_tex,                1e-6,                     {-1, 1, 0, 0, 0, 0, 0}, false, 0.0   },
	[bu_denier]             = { bu_denier,             1.0/9000000.0,            {-1, 1, 0, 0, 0, 0, 0}, false, 0.0   },
	[bu_fluid_dram]         = { bu_fluid_dram,         3.6966911953125e-6,       {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_minim]              = { bu_minim,              6.16115199218750e-8,      {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_peck]               = { bu_peck,               8.80976754172e-3,         {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_bushel]             = { bu_bushel,             3.523907016688e-2,        {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_pfund]              = { bu_pfund,              0.5,                      {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_zentner]            = { bu_zentner,            50.0,                     {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_doppelzentner]      = { bu_doppelzentner,      100.0,                    {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_lot]                = { bu_lot,                15.625e-3,                {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_prussian_line]      = { bu_prussian_line,      3.13853e-1/144.0,         {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_prussian_zoll]      = { bu_prussian_zoll,      3.13853e-1/12.0,          {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_prussian_fuss]      = { bu_prussian_fuss,      3.13853e-1,               {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_prussian_elle]      = { bu_prussian_elle,      6.67160e-1,               {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_prussian_rute]      = { bu_prussian_rute,      3.76624,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_klafter]            = { bu_klafter,            1.88312,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_german_mile]        = { bu_german_mile,        7420.44,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_morgen]             = { bu_morgen,             2553.22,                  {2, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_scheffel]           = { bu_scheffel,           54.961e-3,                {3, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_survey_foot]        = { bu_survey_foot,        1200.0/3937.0,            {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_league]             = { bu_league,             4828.032,                 {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_cable]              = { bu_cable,              185.2,                    {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_hand]               = { bu_hand,               0.1016,                  {1, 0, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_quintal]            = { bu_quintal,            100.0,                    {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_scruple]            = { bu_scruple,            1.2959782e-3,             {0, 1, 0, 0, 0, 0, 0}, false, 0.0    },
	[bu_baud]               = { bu_baud,               1.0,                     {0, 0,-1, 0, 0, 0, 0}, false, 0.0    },
};
#define SI_CONV_TABLE_SIZE \
	((uint32_t)(sizeof(si_conv_table) / sizeof(si_conv_table[0])))
static void bvn_verify_conv_table(void)
{
#ifndef NDEBUG
	for (uint32_t i = 0; i < SI_CONV_TABLE_SIZE; i++) {
		if (bvn_unit_is_currency((int)i))
			continue;
		assert((uint32_t)si_conv_table[i].base == i &&
		       "si_conv_table: .base mismatch at index i");
	}
#endif
}
static const bvn_si_conv_entry_t *bvn_find_si_conv(value_base_unit_t bu)
{
	if ((uint32_t)bu >= SI_CONV_TABLE_SIZE)
		return NULL;
	if (bvn_unit_is_currency((int)bu))
		return NULL;
	return &si_conv_table[bu];
}
double bvn_unit_to_si_factor(value_unit_t u,
			     bool *is_affine,
			     double *affine_offset,
			     bool *ok)
{
	bvn_verify_conv_table();
	double f = 1.0;
	*is_affine     = false;
	*affine_offset = 0.0;
	*ok            = true;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->exponent == exp_invalid) {
			*ok = false;
			return f;
		}
		if (!bvn_prefix_unit_valid(c->prefix, c->base)) {
			*ok = false;
			return f;
		}
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		if (uexp == 0) {
			*ok = false;
			return f;
		}
		int32_t abs_exp = bvni_exp_abs(c->exponent);
		double prefix_contrib = pow(bvni_prefix_factor(*c), (double)abs_exp);
		const bvn_si_conv_entry_t *conv = bvn_find_si_conv(c->base);
		if (!conv) {
			*ok = false;
			return f;
		}
		double bu_contrib = pow(conv->to_si_factor, (double)abs_exp);
		double comp_total = prefix_contrib * bu_contrib;
		if (uexp < 0)
			comp_total = 1.0 / comp_total;
		f *= comp_total;
		if (conv->is_affine) {
			if (uexp == 1) {
				if (*is_affine) {
					*ok = false;
					return f;
				}
				*is_affine     = true;
				*affine_offset = conv->affine_offset;
			} else {
				*ok = false;
				return f;
			}
		}
	}
	return f;
}
bool bvn_unit_dimension_vector(value_unit_t u, int32_t dims[bvn_si_dim_count])
{
	bvn_verify_conv_table();
	memset(dims, 0, sizeof(int32_t) * (size_t)bvn_si_dim_count);
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->exponent == exp_invalid)
			return false;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			return false;
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		if (uexp == 0)
			continue;
		const bvn_si_conv_entry_t *conv = bvn_find_si_conv(c->base);
		if (!conv)
			return false;
		for (int d = 0; d < bvn_si_dim_count; d++) {
			if (conv->dims[d] != 0)
				dims[d] += conv->dims[d] * uexp;
		}
	}
	return true;
}
static bool info_net_exponent(value_unit_t u, value_base_unit_t info_base,
                              int32_t *out)
{
	int32_t sum = 0;
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		if (u.components[i].base == info_base) {
			if (u.components[i].exponent == exp_invalid)
				return false;
			sum += bvn_exponent_to_int(u.components[i].exponent);
		}
	}
	*out = sum;
	return true;
}
bool bvn_units_compatible(value_unit_t a, value_unit_t b)
{
	int32_t a_bit, b_bit, a_byte, b_byte;
	if (!info_net_exponent(a, bu_bit,  &a_bit)  ||
	    !info_net_exponent(b, bu_bit,  &b_bit)  ||
	    !info_net_exponent(a, bu_byte, &a_byte) ||
	    !info_net_exponent(b, bu_byte, &b_byte))
		return false;
	if (a_bit != b_bit || a_byte != b_byte)
		return false;
	int32_t da[bvn_si_dim_count], db[bvn_si_dim_count];
	bool ok_a = bvn_unit_dimension_vector(a, da);
	bool ok_b = bvn_unit_dimension_vector(b, db);
	if (!ok_a || !ok_b)
		return false;
	for (int i = 0; i < bvn_si_dim_count; i++) {
		if (da[i] != db[i])
			return false;
	}
	return true;
}
double bvn_unit_convert_factor(value_unit_t a, value_unit_t b,
			       bool *ok, bool *requires_affine)
{
	*ok             = true;
	*requires_affine = false;
	if (!bvn_units_compatible(a, b)) {
		*ok = false;
		return 0.0;
	}
	bool aff_a = false, aff_b = false;
	double off_a = 0.0, off_b = 0.0;
	bool ok_a = true, ok_b = true;
	double fa = bvn_unit_to_si_factor(a, &aff_a, &off_a, &ok_a);
	double fb = bvn_unit_to_si_factor(b, &aff_b, &off_b, &ok_b);
	if (!ok_a || !ok_b) {
		*ok = false;
		return 0.0;
	}
	if (aff_a || aff_b) {
		*requires_affine = true;
		if (aff_a && aff_b &&
		    fabs(off_a - off_b) <=
		        DBL_EPSILON * fabs(off_a + off_b) + DBL_EPSILON)
			return fa / fb;
		*ok = false;
		return 0.0;
	}
	return fa / fb;
}
value_unit_t bvn_unit_reduce(value_unit_t u, double *scale, bool *overflow)
{
	bvn_verify_conv_table();
	*scale = 1.0;
	if (overflow)
		*overflow = false;
	typedef struct {
		value_base_unit_t base;
		int32_t           exp_sum;
		int32_t           si_pexp_sum;
		int32_t           iec_pexp_sum;
	} accum_t;
	accum_t acc[BVNR_MAX_UNIT_COMPONENTS];
	uint32_t acc_count = 0;
	memset(acc, 0, sizeof(acc));
	for (uint32_t i = 0; i < u.num_components && i < BVNR_MAX_UNIT_COMPONENTS; i++) {
		const value_unit_component_t *c = &u.components[i];
		if (c->base == bu_none)
			continue;
		if (c->exponent == exp_invalid)
			continue;
		if (!bvn_prefix_unit_valid(c->prefix, c->base))
			continue;
		int32_t uexp = bvn_exponent_to_int(c->exponent);
		int32_t pexp = bvni_prefix_exp_int(*c);
		accum_t *a = NULL;
		for (uint32_t j = 0; j < acc_count; j++) {
			if (acc[j].base == c->base) {
				a = &acc[j];
				break;
			}
		}
		if (!a) {
			if (acc_count >= BVNR_MAX_UNIT_COMPONENTS)
				continue;
			a = &acc[acc_count++];
			a->base = c->base;
		}
		a->exp_sum += uexp;
		if (c->prefix.system == prefix_iec)
			a->iec_pexp_sum += pexp;
		else
			a->si_pexp_sum  += pexp;
	}
	value_unit_t result = { .num_components = 0 };
	for (uint32_t ai = 0; ai < acc_count; ai++) {
		accum_t *a = &acc[ai];
		if (a->si_pexp_sum != 0) {
			*scale *= pow(10.0, (double)a->si_pexp_sum);
			if (isinf(*scale) && overflow)
				*overflow = true;
		}
		if (a->iec_pexp_sum != 0) {
			*scale *= pow(2.0,  (double)a->iec_pexp_sum);
			if (isinf(*scale) && overflow)
				*overflow = true;
		}
		if (a->exp_sum == 0)
			continue;
		if (result.num_components >= BVNR_MAX_UNIT_COMPONENTS) {
			if (overflow)
				*overflow = true;
			continue;
		}
		int32_t abs_sum = (a->exp_sum < 0) ? -a->exp_sum : a->exp_sum;
		if (abs_sum > 9) {
			if (overflow)
				*overflow = true;
			const bvn_si_conv_entry_t *conv =
			        bvn_find_si_conv(a->base);
			if (conv) {
				double contrib = pow(conv->to_si_factor, (double)abs_sum);
				if (a->exp_sum < 0)
					*scale /= contrib;
				else
					*scale *= contrib;
			}
			continue;
		}
		value_unit_component_t *rc = &result.components[result.num_components];
		rc->base          = a->base;
		rc->exponent      = bvn_int_to_exponent(a->exp_sum);
		rc->prefix.system = prefix_si;
		rc->prefix.id.si  = si_none;
		result.num_components++;
	}
	for (uint32_t i = 1; i < result.num_components; i++) {
		value_unit_component_t tmp = result.components[i];
		int32_t ei = bvn_exponent_to_int(tmp.exponent);
		uint32_t j = i;
		while (j > 0) {
			int32_t ej = bvn_exponent_to_int(result.components[j - 1].exponent);
			bool should_swap = false;
			if (ei >= 0 && ej < 0) {
				should_swap = true;
			} else if (ej >= 0 && ei < 0) {
				should_swap = false;
			} else {
				int32_t ai_val = (ei < 0) ? -ei : ei;
				int32_t aj_val = (ej < 0) ? -ej : ej;
				if (ai_val != aj_val) {
					should_swap = (ai_val > aj_val);
				} else {
					should_swap = (tmp.base < result.components[j - 1].base);
				}
			}
			if (!should_swap)
				break;
			result.components[j] = result.components[j - 1];
			j--;
		}
		result.components[j] = tmp;
	}
	return result;
}
bool bvn_prefix_unit_valid(value_unit_prefix_t prefix, value_base_unit_t base)
{
	if ((uint32_t)prefix.system >= BVN_PREFIX_SYSTEM_COUNT)
		return false;
	if ((uint32_t)base >= BVN_VALUE_BASE_UNIT_COUNT)
		return false;
	if (bvn_unit_is_currency((int)base))
		return bvn_currency_prefix_valid((int)base, (int)prefix.system);
	bool is_info = (base == bu_bit || base == bu_byte);
	bool is_german = ((uint32_t)base >= (uint32_t)bu_pfund &&
	                  (uint32_t)base <= (uint32_t)bu_scheffel);
	if (is_german) {
		if (prefix.system == prefix_iec)
			return prefix.id.iec == iec_none;
		return prefix.id.si == si_none;
	}
	if (prefix.system == prefix_iec)
		return (prefix.id.iec == iec_none) || is_info;
	if (is_info && prefix.system == prefix_si)
		return prefix.id.si == si_none || prefix.id.si >= si_kilo;
	return true;
}
