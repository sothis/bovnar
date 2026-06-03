
/* bovnar_units.js — faithful JS port of the C core's unit parser
 * (bvn_parse_unit_n) and structural equality (bvn_unit_equal), generated from
 * src/utils/bovnar_utils.c + bovnar_currency.c. Works on UTF-16 char strings
 * (what the editor produces). Exports BovnarUnits.{ parseUnit(str)->{ok,
 * components}, unitEqual(a,b) }. unit_illegal = !parseUnit().ok; unit_mismatch
 * = both parse but !unitEqual (structural, order-sensitive — exactly as the C). */
(function (global) {
  'use strict';
  var BU = [["atmosphere_technical","bu_atmosphere_technical"],["newton_temperature","bu_newton_temp"],["metric_horsepower","bu_metric_horsepower"],["preussischer_fuss","bu_prussian_fuss"],["prussian_scheffel","bu_scheffel"],["standard_gravity","bu_standard_gravity"],["prussian_klafter","bu_klafter"],["preussische_rute","bu_prussian_rute"],["preussische_elle","bu_prussian_elle"],["fluid_ounces_uk","bu_fluid_ounce_uk"],["prussian_morgen","bu_morgen"],["nautical_miles","bu_nautical_mile"],["fluid_ounce_uk","bu_fluid_ounce_uk"],["kilogram_force","bu_kilogram_force"],["deutsche_meile","bu_german_mile"],["per_cent_mille","bu_per_cent_mille"],["nautical_mile","bu_nautical_mile"],["electronvolts","bu_electronvolt"],["doppelzentner","bu_doppelzentner"],["prussian_zoll","bu_prussian_zoll"],["prussian_rute","bu_prussian_rute"],["prussian_line","bu_prussian_line"],["prussian_fuss","bu_prussian_fuss"],["prussian_elle","bu_prussian_elle"],["pennyweights","bu_pennyweight"],["fluid_ounces","bu_fluid_ounce"],["electronvolt","bu_electronvolt"],["volt_amperes","bu_volt_ampere"],["inch_mercury","bu_inch_hg"],["survey_foot","bu_survey_foot"],["foot_pounds","bu_foot_pound"],["german_mile","bu_german_mile"],["volt_ampere","bu_volt_ampere"],["atmospheres","bu_atmosphere"],["tablespoons","bu_tablespoon"],["troy_ounces","bu_troy_ounce"],["fluid_ounce","bu_fluid_ounce"],["pound_force","bu_pound_force"],["light_years","bu_light_year"],["pennyweight","bu_pennyweight"],["revolutions","bu_revolution"],["fluid_drams","bu_fluid_dram"],["foot_pound","bu_foot_pound"],["steradians","bu_steradian"],["becquerels","bu_becquerel"],["short_tons","bu_short_ton"],["gallons_uk","bu_gallon_uk"],["tablespoon","bu_tablespoon"],["horsepower","bu_horsepower"],["fahrenheit","bu_fahrenheit"],["atmosphere","bu_atmosphere"],["troy_ounce","bu_troy_ounce"],["light_year","bu_light_year"],["arcminutes","bu_arcminute"],["arcseconds","bu_arcsecond"],["fortnights","bu_fortnight"],["revolution","bu_revolution"],["fluid_dram","bu_fluid_dram"],["per_myriad","bu_per_myriad"],["teaspoons","bu_teaspoon"],["short_ton","bu_short_ton"],["gallon_uk","bu_gallon_uk"],["angstroms","bu_angstrom"],["arcminute","bu_arcminute"],["arcsecond","bu_arcsecond"],["long_tons","bu_long_ton"],["roentgens","bu_roentgen"],["steradian","bu_steradian"],["becquerel","bu_becquerel"],["quarts_uk","bu_quart_uk"],["fortnight","bu_fortnight"],["degreesRa","bu_rankine"],["degreesDe","bu_delisle"],["degreesRe","bu_reaumur"],["degreesRo","bu_romer"],["per_mille","bu_per_mille"],["gills_uk","bu_gill_uk"],["quintals","bu_quintal"],["scruples","bu_scruple"],["decibels","bu_decibel"],["teaspoon","bu_teaspoon"],["angstrom","bu_angstrom"],["furlongs","bu_furlong"],["maxwells","bu_maxwell"],["oersteds","bu_oersted"],["galileos","bu_galileo"],["long_ton","bu_long_ton"],["roentgen","bu_roentgen"],["calories","bu_calorie"],["gradians","bu_grad"],["hectares","bu_hectare"],["candelas","bu_candela"],["coulombs","bu_coulomb"],["sieverts","bu_sievert"],["pints_uk","bu_pint_uk"],["quart_uk","bu_quart_uk"],["fl_oz_uk","bu_fluid_ounce_uk"],["fl_drams","bu_fluid_dram"],["scheffel","bu_scheffel"],["degreesF","bu_fahrenheit"],["degreesC","bu_celsius"],["degreeRa","bu_rankine"],["degreeDe","bu_delisle"],["degreeRe","bu_reaumur"],["degreeRo","bu_romer"],["degreesN","bu_newton_temp"],["inch_hg","bu_inch_hg"],["gill_uk","bu_gill_uk"],["percent","bu_percent"],["leagues","bu_league"],["quintal","bu_quintal"],["scruple","bu_scruple"],["bushels","bu_bushel"],["deniers","bu_denier"],["zentner","bu_zentner"],["klafter","bu_klafter"],["rankine","bu_rankine"],["decibel","bu_decibel"],["delisle","bu_delisle"],["reaumur","bu_reaumur"],["degreeN","bu_newton_temp"],["gallons","bu_gallon"],["parsecs","bu_parsec"],["furlong","bu_furlong"],["maxwell","bu_maxwell"],["oersted","bu_oersted"],["galileo","bu_galileo"],["calorie","bu_calorie"],["fathoms","bu_fathom"],["barrels","bu_barrel"],["gradian","bu_grad"],["radians","bu_radian"],["daltons","bu_dalton"],["seconds","bu_second"],["amperes","bu_ampere"],["kelvins","bu_kelvin"],["candela","bu_candela"],["newtons","bu_newton"],["pascals","bu_pascal"],["coulomb","bu_coulomb"],["siemens","bu_siemens"],["henries","bu_henry"],["celsius","bu_celsius"],["degreeF","bu_fahrenheit"],["degreeC","bu_celsius"],["minutes","bu_minute"],["hectare","bu_hectare"],["sievert","bu_sievert"],["degrees","bu_degree"],["pint_uk","bu_pint_uk"],["bushel","bu_bushel"],["cables","bu_cable"],["league","bu_league"],["denier","bu_denier"],["months","bu_month"],["pfunds","bu_pfund"],["schffl","bu_scheffel"],["morgen","bu_morgen"],["minims","bu_minim"],["chains","bu_chain"],["nepers","bu_neper"],["barrel","bu_barrel"],["arcmin","bu_arcminute"],["arcsec","bu_arcsecond"],["parsec","bu_parsec"],["gallon","bu_gallon"],["fathom","bu_fathom"],["pounds","bu_pound"],["ounces","bu_ounce"],["grains","bu_grain"],["stones","bu_stone"],["carats","bu_carat"],["therms","bu_therm"],["quarts","bu_quart"],["stilbs","bu_stilb"],["curies","bu_curie"],["poises","bu_poise"],["stokes","bu_stokes"],["gal_uk","bu_gallon_uk"],["radian","bu_radian"],["tonnes","bu_tonne"],["dalton","bu_dalton"],["second","bu_second"],["metres","bu_meter"],["meters","bu_meter"],["ampere","bu_ampere"],["kelvin","bu_kelvin"],["newton","bu_newton"],["pascal","bu_pascal"],["joules","bu_joule"],["farads","bu_farad"],["webers","bu_weber"],["teslas","bu_tesla"],["henrys","bu_henry"],["lumens","bu_lumen"],["katals","bu_katal"],["litres","bu_liter"],["liters","bu_liter"],["minute","bu_minute"],["degree","bu_degree"],["degrRa","bu_rankine"],["degrDe","bu_delisle"],["degrRe","bu_reaumur"],["degrRo","bu_romer"],["inches","bu_inch"],["gi_uk","bu_gill_uk"],["bauds","bu_baud"],["cable","bu_cable"],["hands","bu_hand"],["pecks","bu_peck"],["turns","bu_revolution"],["minim","bu_minim"],["month","bu_month"],["fl_dr","bu_fluid_dram"],["linie","bu_prussian_line"],["pfund","bu_pfund"],["dt_mi","bu_german_mile"],["drams","bu_dram"],["slugs","bu_slug"],["chain","bu_chain"],["gills","bu_gill"],["neper","bu_neper"],["gauss","bu_gauss"],["poise","bu_poise"],["stilb","bu_stilb"],["curie","bu_curie"],["stoke","bu_stokes"],["therm","bu_therm"],["quart","bu_quart"],["pound","bu_pound"],["ounce","bu_ounce"],["grain","bu_grain"],["stone","bu_stone"],["carat","bu_carat"],["yards","bu_yard"],["miles","bu_mile"],["acres","bu_acre"],["barns","bu_barn"],["dynes","bu_dyne"],["phots","bu_phot"],["knots","bu_knot"],["pints","bu_pint"],["degrF","bu_fahrenheit"],["degRa","bu_rankine"],["degrN","bu_newton_temp"],["degDe","bu_delisle"],["degRe","bu_reaumur"],["degRo","bu_romer"],["romer","bu_romer"],["tonne","bu_tonne"],["metre","bu_meter"],["meter","bu_meter"],["grams","bu_gram"],["joule","bu_joule"],["farad","bu_farad"],["weber","bu_weber"],["tesla","bu_tesla"],["henry","bu_henry"],["lumen","bu_lumen"],["katal","bu_katal"],["litre","bu_liter"],["liter","bu_liter"],["weeks","bu_week"],["years","bu_year"],["moles","bu_mol"],["hertz","bu_hertz"],["watts","bu_watt"],["volts","bu_volt"],["grays","bu_gray"],["hours","bu_hour"],["Bytes","bu_byte"],["bytes","bu_byte"],["degrC","bu_celsius"],["tn_sh","bu_short_ton"],["fl_oz","bu_fluid_ounce"],["pt_uk","bu_pint_uk"],["qt_uk","bu_quart_uk"],["ft_lb","bu_foot_pound"],["ftUS","bu_survey_foot"],["qntl","bu_quintal"],["baud","bu_baud"],["hand","bu_hand"],["rods","bu_rod"],["slug","bu_slug"],["gill","bu_gill"],["cups","bu_cup"],["kips","bu_kip"],["prln","bu_prussian_line"],["lots","bu_lot"],["rute","bu_prussian_rute"],["elle","bu_prussian_elle"],["zoll","bu_prussian_zoll"],["knot","bu_knot"],["rems","bu_rem"],["pint","bu_pint"],["barn","bu_barn"],["acre","bu_acre"],["dyne","bu_dyne"],["phot","bu_phot"],["tbsp","bu_tablespoon"],["tn_l","bu_long_ton"],["degF","bu_fahrenheit"],["Torr","bu_torr"],["torr","bu_torr"],["grad","bu_grad"],["mmHg","bu_mmhg"],["oz_t","bu_troy_ounce"],["bars","bu_bar"],["week","bu_week"],["year","bu_year"],["gram","bu_gram"],["amps","bu_ampere"],["mole","bu_mol"],["watt","bu_watt"],["volt","bu_volt"],["ohms","bu_ohm"],["days","bu_day"],["hour","bu_hour"],["Byte","bu_byte"],["byte","bu_byte"],["thou","bu_thou"],["mils","bu_thou"],["bits","bu_bit"],["gray","bu_gray"],["degC","bu_celsius"],["degN","bu_newton_temp"],["degr","bu_degree"],["inch","bu_inch"],["foot","bu_foot"],["feet","bu_foot"],["yard","bu_yard"],["mile","bu_mile"],["fath","bu_fathom"],["ergs","bu_erg"],["vars","bu_var"],["dram","bu_dram"],["peck","bu_peck"],["turn","bu_revolution"],["inHg","bu_inch_hg"],["°Ra","bu_rankine"],["°De","bu_delisle"],["°Re","bu_reaumur"],["°Ro","bu_romer"],["pcm","bu_per_cent_mille"],["ppm","bu_ppm"],["ppb","bu_ppb"],["lea","bu_league"],["cbl","bu_cable"],["dwt","bu_pennyweight"],["rpm","bu_rpm"],["kgf","bu_kilogram_force"],["var","bu_var"],["gal","bu_gallon"],["Gal","bu_galileo"],["Btu","bu_btu"],["BTU","bu_btu"],["btu","bu_btu"],["lbf","bu_pound_force"],["lbs","bu_pound"],["psi","bu_psi"],["atm","bu_atmosphere"],["cal","bu_calorie"],["erg","bu_erg"],["thm","bu_therm"],["kip","bu_kip"],["rem","bu_rem"],["cup","bu_cup"],["gon","bu_grad"],["bbl","bu_barrel"],["fur","bu_furlong"],["dyn","bu_dyne"],["nmi","bu_nautical_mile"],["mil","bu_thou"],["rod","bu_rod"],["tex","bu_tex"],["den","bu_denier"],["bsh","bu_bushel"],["rev","bu_revolution"],["Pfd","bu_pfund"],["prz","bu_prussian_zoll"],["prf","bu_prussian_fuss"],["Ztr","bu_zentner"],["lot","bu_lot"],["deg","bu_degree"],["bar","bu_bar"],["sec","bu_second"],["amp","bu_ampere"],["ohm","bu_ohm"],["Ohm","bu_ohm"],["lux","bu_lux"],["day","bu_day"],["bit","bu_bit"],["kat","bu_katal"],["min","bu_minute"],["mol","bu_mol"],["rad","bu_radian"],["tsp","bu_teaspoon"],["ch","bu_chain"],["rd","bu_rod"],["gi","bu_gill"],["dr","bu_dram"],["°N","bu_newton_temp"],["°C","bu_celsius"],["°F","bu_fahrenheit"],["Bd","bu_baud"],["sc","bu_scruple"],["Ra","bu_rankine"],["VA","bu_volt_ampere"],["dB","bu_decibel"],["Oe","bu_oersted"],["Mx","bu_maxwell"],["Ci","bu_curie"],["Np","bu_neper"],["sb","bu_stilb"],["ph","bu_phot"],["St","bu_stokes"],["ly","bu_light_year"],["pc","bu_parsec"],["ac","bu_acre"],["kn","bu_knot"],["qt","bu_quart"],["pt","bu_pint"],["ft","bu_foot"],["yd","bu_yard"],["mi","bu_mile"],["lb","bu_pound"],["oz","bu_ounce"],["gr","bu_grain"],["st","bu_stone"],["ct","bu_carat"],["hp","bu_horsepower"],["in","bu_inch"],["Bq","bu_becquerel"],["Gy","bu_gray"],["Hz","bu_hertz"],["Pa","bu_pascal"],["Sv","bu_sievert"],["Wb","bu_weber"],["cd","bu_candela"],["lm","bu_lumen"],["lx","bu_lux"],["sr","bu_steradian"],["ha","bu_hectare"],["wk","bu_week"],["yr","bu_year"],["au","bu_astronomical_unit"],["Da","bu_dalton"],["eV","bu_electronvolt"],["gn","bu_standard_gravity"],["PS","bu_metric_horsepower"],["CV","bu_metric_horsepower"],["mo","bu_month"],["fn","bu_fortnight"],["pk","bu_peck"],["at","bu_atmosphere_technical"],["dz","bu_doppelzentner"],["‰","bu_per_mille"],["‱","bu_per_myriad"],["Å","bu_angstrom"],["Ω","bu_ohm"],["Å","bu_angstrom"],["°","bu_degree"],["%","bu_percent"],["G","bu_gauss"],["P","bu_poise"],["R","bu_roentgen"],["B","bu_byte"],["C","bu_coulomb"],["F","bu_farad"],["H","bu_henry"],["J","bu_joule"],["K","bu_kelvin"],["L","bu_liter"],["N","bu_newton"],["l","bu_liter"],["S","bu_siemens"],["T","bu_tesla"],["V","bu_volt"],["W","bu_watt"],["A","bu_ampere"],["b","bu_bit"],["d","bu_day"],["g","bu_gram"],["h","bu_hour"],["m","bu_meter"],["s","bu_second"],["t","bu_tonne"]];          /* [name, bu_id] sorted by descending name length  */
  var SI = {"Q":"si_quetta","R":"si_ronna","Y":"si_yotta","Z":"si_zetta","E":"si_exa","P":"si_peta","T":"si_tera","G":"si_giga","M":"si_mega","k":"si_kilo","h":"si_hecto","da":"si_deca","d":"si_deci","c":"si_centi","m":"si_milli","µ":"si_micro","n":"si_nano","p":"si_pico","f":"si_femto","a":"si_atto","z":"si_zepto","y":"si_yocto","r":"si_ronto","q":"si_quecto"};          /* prefix symbol -> si_id                          */
  var IEC = {"Qi":"iec_quebi","Ri":"iec_robi","Yi":"iec_yobi","Zi":"iec_zebi","Ei":"iec_exbi","Pi":"iec_pebi","Ti":"iec_tebi","Gi":"iec_gibi","Mi":"iec_mebi","Ki":"iec_kibi"};        /* prefix symbol -> iec_id                         */
  var CURSET = (function () { var s = Object.create(null); ["AED","AFN","ALL","AMD","ANG","AOA","ARS","AUD","AWG","AZN","BAM","BBD","BDT","BGN","BHD","BIF","BMD","BND","BOB","BRL","BSD","BTN","BWP","BYN","BZD","CAD","CDF","CHF","CLF","CLP","CNY","COP","CRC","CUP","CVE","CZK","DJF","DKK","DOP","DZD","EGP","ERN","ETB","EUR","FJD","FKP","GBP","GEL","GHS","GIP","GMD","GNF","GTQ","GYD","HKD","HNL","HRK","HTG","HUF","IDR","ILS","INR","IQD","IRR","ISK","JMD","JOD","JPY","KES","KGS","KHR","KMF","KPW","KRW","KWD","KYD","KZT","LAK","LBP","LKR","LRD","LSL","LYD","MAD","MDL","MGA","MKD","MMK","MNT","MOP","MRU","MUR","MVR","MWK","MXN","MYR","MZN","NAD","NGN","NIO","NOK","NPR","NZD","OMR","PAB","PEN","PGK","PHP","PKR","PLN","PYG","QAR","RON","RSD","RUB","RWF","SAR","SBD","SCR","SDG","SEK","SGD","SHP","SLE","SLL","SOS","SSP","SRD","STN","SVC","SYP","SZL","THB","TJS","TMT","TND","TOP","TRY","TTD","TWD","TZS","UAH","UGX","USD","UYU","UZS","VES","VND","VUV","WST","XAF","XAG","XAU","XCD","XDR","XOF","XPD","XPF","XPT","XTS","YER","ZAR","ZMW","ZWL","BTC","ETH","SOL","XRP","BNB","ADA","LTC","DOT","XMR","ETC","BCH","XLM","FIL","ICP","TRX","EOS","VET","NEO","ZEC","UNI","ARB","SUI","TON","INJ","SEI","APT","TAO","WIF","DOGE","LINK","USDT","USDC","AVAX","ATOM","POL","NEAR","ALGO","HBAR","AAVE","MKR","DAI","STX","GRT","LDO","BONK","PEPE","SHIB","JUP","PYTH","RUNE"].forEach(function (c) { s[c] = 1; }); return s; })();

  /* si_prefix_id_e order (bovnar.h) — for the "info unit needs >= kilo" rule */
  var SI_ORDER = {}; ['si_none','si_quecto','si_ronto','si_yocto','si_zepto','si_atto',
    'si_femto','si_pico','si_nano','si_micro','si_milli','si_centi','si_deci',
    'si_deca','si_hecto','si_kilo','si_mega','si_giga','si_tera','si_peta','si_exa',
    'si_zetta','si_yotta','si_ronna','si_quetta'].forEach(function (n, i) { SI_ORDER[n] = i; });

  var GERMAN = { bu_pfund:1, bu_zentner:1, bu_doppelzentner:1, bu_lot:1,
    bu_prussian_line:1, bu_prussian_zoll:1, bu_prussian_fuss:1, bu_prussian_elle:1,
    bu_prussian_rute:1, bu_klafter:1, bu_german_mile:1, bu_morgen:1, bu_scheffel:1 };
  var RATIO = { bu_percent:1, bu_per_mille:1, bu_per_myriad:1, bu_per_cent_mille:1, bu_ppm:1, bu_ppb:1 };

  function isCurrencyBase(b) { return b.lastIndexOf('cur_', 0) === 0; }

  /* bvn_prefix_unit_valid */
  function prefixUnitValid(p, base) {
    if (isCurrencyBase(base)) return p.system !== 'iec';         /* currency: SI ok, IEC no */
    var isInfo = (base === 'bu_bit' || base === 'bu_byte');
    if (GERMAN[base] || RATIO[base])
      return p.system === 'iec' ? p.id === 'iec_none' : p.id === 'si_none';
    if (p.system === 'iec') return p.id === 'iec_none' || isInfo;
    if (isInfo && p.system === 'si')
      return p.id === 'si_none' || SI_ORDER[p.id] >= SI_ORDER.si_kilo;
    return true;
  }

  var SUP = { '⁰':0,'¹':1,'²':2,'³':3,'⁴':4,'⁵':5,
              '⁶':6,'⁷':7,'⁸':8,'⁹':9 };

  /* parse_unit_exponent_suffix -> { exp, used } (chars consumed from the end) */
  function parseExpSuffix(s) {
    var n = s.length;
    if (n >= 2 && s[n - 2] === '^') { var d = s[n - 1]; if (d >= '1' && d <= '9') return { exp: +d, used: 2 }; }
    if (n >= 3 && s[n - 3] === '^' && (s[n - 2] === '+' || s[n - 2] === '-')) {
      var d2 = s[n - 1]; if (d2 >= '1' && d2 <= '9') return { exp: (s[n - 2] === '-' ? -1 : 1) * (+d2), used: 3 };
    }
    var last = s[n - 1], v = SUP[last];
    if (v !== undefined && v >= 1) {
      var neg = false, sign = 0, p = s[n - 2];
      if (p === '⁻') { neg = true; sign = 1; } else if (p === '⁺') { sign = 1; }
      return { exp: neg ? -v : v, used: 1 + sign };
    }
    return { exp: 1, used: 0 };
  }

  /* parse_single_unit_component */
  function parseComponent(s) {
    var e = parseExpSuffix(s); var exp = e.exp;
    s = s.slice(0, s.length - e.used);
    if (!s.length) return null;
    if (s[0] === '$') {                       /* $CODE currency */
      var code = s.slice(1);
      if (CURSET[code]) return { base: 'cur_' + code, exponent: exp, prefix: { system: 'si', id: 'si_none' } };
      return null;
    }
    for (var ti = 1; ti + 1 < s.length; ti++) {        /* prefix~$CODE */
      if (s[ti] !== '~') continue;
      if (s[ti + 1] !== '$') break;
      var code2 = s.slice(ti + 2);
      if (!CURSET[code2]) return null;
      var base = 'cur_' + code2, pfx = s.slice(0, ti);
      if (IEC[pfx]) { var pi = { system: 'iec', id: IEC[pfx] }; return prefixUnitValid(pi, base) ? { base: base, exponent: exp, prefix: pi } : null; }
      if (SI[pfx])  { var ps = { system: 'si',  id: SI[pfx]  }; return prefixUnitValid(ps, base) ? { base: base, exponent: exp, prefix: ps } : null; }
      return null;
    }
    var best = null;                          /* physical unit: longest base suffix */
    for (var i = 0; i < BU.length; i++) {
      var name = BU[i][0];
      if (name.length > s.length) continue;
      if (s.slice(s.length - name.length) === name) { best = BU[i]; break; }
    }
    if (!best) return null;
    var b = best[1], plen = s.length - best[0].length;
    if (plen === 0) return { base: b, exponent: exp, prefix: { system: 'si', id: 'si_none' } };
    if (plen < 2 || s[plen - 1] !== '~') return null;
    var pf = s.slice(0, plen - 1);
    if (IEC[pf]) { var qi = { system: 'iec', id: IEC[pf] }; return prefixUnitValid(qi, b) ? { base: b, exponent: exp, prefix: qi } : null; }
    if (SI[pf])  { var qs = { system: 'si',  id: SI[pf]  }; return prefixUnitValid(qs, b) ? { base: b, exponent: exp, prefix: qs } : null; }
    return null;
  }

  /* bvn_parse_unit_expr (compound: * / · and parenthesised groups) */
  function parseExpr(s, depth) {
    if (!s) return null;
    if (depth > 16) return null;
    if (s === 'no_unit') return { components: [] };
    var comps = [], inDenom = false, i = 0;
    while (i < s.length) {
      if (s[i] === '(') {
        var pd = 1, j = i + 1;
        for (; j < s.length && pd; j++) { if (s[j] === '(') pd++; else if (s[j] === ')') pd--; if (pd === 0) break; }
        if (pd !== 0) return null;
        var g = parseExpr(s.slice(i + 1, j), depth + 1);
        if (!g) return null;
        for (var k = 0; k < g.components.length; k++) {
          if (comps.length >= 8) return null;
          var gc = g.components[k];
          comps.push({ base: gc.base, exponent: inDenom ? -gc.exponent : gc.exponent, prefix: gc.prefix });
        }
        i = j + 1;
      } else {
        var start = i;
        while (i < s.length) { var b = s[i]; if (b === '*' || b === '/' || b === '(' || b === '·') break; i++; }
        if (i === start) return null;
        var c = parseComponent(s.slice(start, i));
        if (!c) return null;
        if (inDenom) c.exponent = -c.exponent;
        if (comps.length >= 8) return null;
        comps.push(c);
      }
      if (i < s.length) {
        var sep = s[i];
        if (sep === '/') { inDenom = true; i++; }
        else if (sep === '*' || sep === '·') { i++; }
        else return null;
        if (i >= s.length) return null;          /* trailing separator */
      }
    }
    if (!comps.length) return null;
    return { components: comps };
  }

  function parseUnit(str) {
    if (str == null || str === '') return { ok: false, components: [] };
    var r = parseExpr(String(str), 0);
    return r ? { ok: true, components: r.components } : { ok: false, components: [] };
  }
  function unitEqual(a, b) {                    /* structural, order-sensitive */
    if (a.components.length !== b.components.length) return false;
    for (var i = 0; i < a.components.length; i++) {
      var ca = a.components[i], cb = b.components[i];
      if (ca.base !== cb.base || ca.exponent !== cb.exponent) return false;
      if (ca.prefix.system !== cb.prefix.system || ca.prefix.id !== cb.prefix.id) return false;
    }
    return true;
  }

  var api = { parseUnit: parseUnit, unitEqual: unitEqual };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else global.BovnarUnits = api;
}(typeof window !== 'undefined' ? window : this));
