syn clear

hi BovnarComment      ctermfg=240 guifg=#6e7a86 gui=none
hi BovnarKeySigil     ctermfg=135 guifg=#c084fc
hi BovnarKeyName      ctermfg=39  guifg=#67d1f4
hi BovnarSemicolon    ctermfg=66  guifg=#5a6270 gui=none
hi BovnarAssign       ctermfg=203 guifg=#f47067
hi BovnarType         ctermfg=255 guifg=#e8e8e8 gui=bold
hi BovnarTypeDelim    ctermfg=186 guifg=#a09040 gui=none
hi BovnarTypeSep      ctermfg=246 guifg=#6e7a86 gui=none
hi BovnarTypeParam    ctermfg=209 guifg=#da844c gui=none
hi BovnarTypeBitWidth ctermfg=205 guifg=#ff5faf gui=none
hi BovnarTypeBase     ctermfg=158 guifg=#afffd7 gui=none
hi BovnarNoUnit       ctermfg=114 guifg=#87d88c gui=none
hi BovnarSpecialFloat ctermfg=170 guifg=#c084fc
hi BovnarFloat        ctermfg=220 guifg=#f0c64e gui=none
hi BovnarInteger      ctermfg=220 guifg=#f0c64e gui=none
hi BovnarNegative     ctermfg=203 guifg=#f47067 gui=none
hi BovnarString       ctermfg=114 guifg=#87d88c gui=none
hi BovnarEscape       ctermfg=72  guifg=#5ab06e gui=bold
hi BovnarInvalidEsc   ctermfg=203 guifg=#f47067 gui=undercurl
hi BovnarSymbol       ctermfg=39  guifg=#67d1f4 gui=none
hi BovnarRefOp        ctermfg=203 guifg=#f47067 gui=bold
hi BovnarRefPath      ctermfg=214 guifg=#d06070 gui=none
hi BovnarArrayDelim1  ctermfg=209 guifg=#da844c gui=none
hi BovnarArrayDelim2  ctermfg=135 guifg=#c084fc gui=none
hi BovnarArrayDelim3  ctermfg=200 guifg=#ff2a7d gui=none
hi BovnarArrayDelim4  ctermfg=214 guifg=#ffa500 gui=none
hi BovnarArrayDelim5  ctermfg=46  guifg=#87d88c gui=none
hi BovnarArraySep     ctermfg=66  guifg=#6e7a86 gui=none
hi BovnarRowSep       ctermfg=66  guifg=#6e7a86 gui=none
hi BovnarUnitSep      ctermfg=66  guifg=#6e7a86 gui=none
hi BovnarUnitPrefix   ctermfg=80  guifg=#5eead4 gui=none
hi BovnarUnitTilde    ctermfg=66  guifg=#6e7a86 gui=none
hi BovnarUnitExp      ctermfg=80  guifg=#5eead4 gui=none
hi BovnarTypeUnit     ctermfg=80  guifg=#5eead4 gui=none
hi BovnarTypeUnitSep  ctermfg=66  guifg=#6e7a86 gui=none
hi BovnarTypeUnitExp  ctermfg=80  guifg=#5eead4 gui=none
hi BovnarTypePrefix   ctermfg=80  guifg=#5eead4 gui=none
hi BovnarOctetData    ctermfg=102 guifg=#6272a4 gui=italic
hi BovnarNull         ctermfg=240 guifg=#4c5661 gui=italic
hi BovnarBoolean      ctermfg=170 guifg=#c084fc gui=bold

" 8 cycling colors for { } depth — levels 1,9,17,… share color 1; 2,10,18,… color 2; etc.
hi BovnarStructDelim1 ctermfg=209 guifg=#da844c gui=none
hi BovnarStructDelim2 ctermfg=135 guifg=#c084fc gui=none
hi BovnarStructDelim3 ctermfg=200 guifg=#ff2a7d gui=none
hi BovnarStructDelim4 ctermfg=214 guifg=#ffa500 gui=none
hi BovnarStructDelim5 ctermfg=46  guifg=#87d88c gui=none
hi BovnarStructDelim6 ctermfg=80  guifg=#5eead4 gui=none
hi BovnarStructDelim7 ctermfg=220 guifg=#f0c64e gui=none
hi BovnarStructDelim8 ctermfg=39  guifg=#67d1f4 gui=none

syn match   BovnarOctetData    '\\x[0-9A-Fa-f]\{2\}'
" $nan  $inf  $-inf — single leading sigil, no trailing $; \> rejects a
" trailing identifier byte so $infinity (an error) is not matched.
syn match   BovnarSpecialFloat '\$\(nan\|-inf\|inf\)\>'

syn region  BovnarComment start=/#/ end=/$/

syn match   BovnarArraySep    ','
syn match   BovnarRowSep      '/'
syn match   BovnarKeySigil    '\.'

syn match   BovnarNegative '-\(\d\+\.\d*\([eE][+-]\=\d\+\)\=\|\.\d\+\([eE][+-]\=\d\+\)\=\|\d\+[eE][+-]\=\d\+\|\d\+\)' contains=BovnarFloat,BovnarInteger

syn match   BovnarAssign    '='
syn match   BovnarSemicolon ';'

syn match   BovnarNull '=\s*;'

syn match   BovnarInteger '\d\+'
syn match   BovnarFloat   '\d\+\.\d*\([eE][+-]\=\d\+\)\?'
syn match   BovnarFloat   '\d\+[eE][+-]\=\d\+'
syn match   BovnarFloat   '\.\d\+\([eE][+-]\=\d\+\)\?'

syn region  BovnarString
      \ start=/"/ skip=/\\./ end=/"/
      \ contains=BovnarEscape,BovnarInvalidEsc
syn match   BovnarEscape     '\\[tnvfr"\\]'
syn match   BovnarInvalidEsc '\\.'

syn match   BovnarRefOp   '&'
syn match   BovnarRefPath '&\(\.[A-Za-z_][A-Za-z0-9_+\-]*\)\+'

syn region  BovnarTypeAnn
      \ start=/</ end=/>/
      \ contains=BovnarType,BovnarTypeParam,BovnarNoUnit,
      \          BovnarTypeDelim,BovnarTypeSep,BovnarComment,
      \          BovnarTypeBitWidth,BovnarTypeBase,
      \          BovnarTypeUnit,BovnarTypeUnitSep,
      \          BovnarTypeUnitExp,BovnarTypePrefix,
      \          BovnarUnitPrefix,BovnarUnitSep,
      \          BovnarString,
      \          BovnarRefPath,BovnarArraySep
      \ keepend

syn match   BovnarUnitTilde /\~/
syn match   BovnarSymbol    '\<[A-Za-z_][A-Za-z0-9_+\-]*\>'
" Reserved value keywords — defined after BovnarSymbol so they win the tie and
" are coloured as keywords, not symbols. on==true, off==false; null is a null.
syn match   BovnarBoolean   '\<\(true\|false\|on\|off\)\>'
syn match   BovnarNullKw    '\<null\>'
hi link     BovnarNullKw    BovnarNull
syn match   BovnarTypeUnit  '\<[A-Za-zµΩ°][A-Za-z0-9_·()\-\+\/\^]*\>'
syn match   BovnarUnitSep   '[()]'
syn match   BovnarTypeUnit  '[%‰‱]'
" Currency component carries a mandatory '$' sigil (spec 1.0): $USD, $BTC.
syn match   BovnarTypeUnit  '\$[A-Za-z][A-Za-z0-9]*'
syn match   BovnarUnitPrefix '[A-Za-zµ][A-Za-z0-9]*\~\$\=[A-Za-z0-9_·\-\+\/*\^]*'
syn match   BovnarUnitSep   '[*\/]'
syn match   BovnarUnitExp   '[⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻]'
syn match   BovnarUnitExp   '\^[+-]\=\d\+'

syn match   BovnarType     '\v(float_fix|float_dec|float|uint|sint|utf8|bool)'
syn match   BovnarNoUnit   'no_unit'
syn match   BovnarTypeDelim '[<>]' contained
syn match   BovnarTypeSep  ':' contained
syn match   BovnarTypeParam    'q[0-9]\+' contained
syn match   BovnarTypeBitWidth '[0-9]\+'  contained
syn match   BovnarTypeBase     '_[0-9]\+' contained

syn match   BovnarTypeUnitSep '[*\/]'    contained
syn match   BovnarTypeUnitExp '[⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻]' contained
syn match   BovnarTypeUnitExp '\^[+-]\=\d\+' contained
syn match   BovnarTypeUnitExp '\/[1-9]'      contained
syn match   BovnarTypePrefix  '[A-Za-zµ][A-Za-z0-9]*\~' contained

syn match   BovnarKeyName '\.[A-Za-z_][A-Za-z0-9_+\-]*'

" Terminal items only — no struct or array regions.
" Both BovnarArray1 and BovnarStruct1 are non-contained and are added explicitly
" to the contains lists of each array/struct region below.
syn cluster BovnarContent contains=
      \ BovnarComment,BovnarKeySigil,BovnarKeyName,
      \ BovnarSemicolon,BovnarAssign,BovnarTypeAnn,
      \ BovnarNegative,BovnarFloat,BovnarInteger,
      \ BovnarString,BovnarEscape,BovnarInvalidEsc,
      \ BovnarSymbol,BovnarBoolean,BovnarNullKw,
      \ BovnarRefOp,BovnarRefPath,
      \ BovnarArraySep,BovnarRowSep,
      \ BovnarNull,BovnarSpecialFloat,BovnarOctetData,
      \ BovnarUnitTilde,BovnarUnitSep,BovnarUnitExp,
      \ BovnarUnitPrefix,BovnarNoUnit,BovnarTypeUnit,
      \ BovnarType

" 5-level array region chain.
" BovnarArray1 is not contained — it starts at the top level and inside struct regions.
" Each level's matchgroup colors both [ and ] with the same highlight group.
" Struct depth resets to 1 when entering an array context.
syn region BovnarArray1 matchgroup=BovnarArrayDelim1 start=/\[/ end=/\]/ contains=@BovnarContent,BovnarArray2,BovnarStruct1
syn region BovnarArray2 matchgroup=BovnarArrayDelim2 start=/\[/ end=/\]/ contained contains=@BovnarContent,BovnarArray3,BovnarStruct1
syn region BovnarArray3 matchgroup=BovnarArrayDelim3 start=/\[/ end=/\]/ contained contains=@BovnarContent,BovnarArray4,BovnarStruct1
syn region BovnarArray4 matchgroup=BovnarArrayDelim4 start=/\[/ end=/\]/ contained contains=@BovnarContent,BovnarArray5,BovnarStruct1
syn region BovnarArray5 matchgroup=BovnarArrayDelim5 start=/\[/ end=/\]/ contained contains=@BovnarContent,BovnarStruct1

" 64-level struct region chain, 8-color cycling.
" BovnarArray1 is listed explicitly so arrays inside structs are highlighted.
syn region BovnarStruct1  matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contains=@BovnarContent,BovnarStruct2,BovnarArray1
syn region BovnarStruct2  matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct3,BovnarArray1
syn region BovnarStruct3  matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct4,BovnarArray1
syn region BovnarStruct4  matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct5,BovnarArray1
syn region BovnarStruct5  matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct6,BovnarArray1
syn region BovnarStruct6  matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct7,BovnarArray1
syn region BovnarStruct7  matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct8,BovnarArray1
syn region BovnarStruct8  matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct9,BovnarArray1
syn region BovnarStruct9  matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct10,BovnarArray1
syn region BovnarStruct10 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct11,BovnarArray1
syn region BovnarStruct11 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct12,BovnarArray1
syn region BovnarStruct12 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct13,BovnarArray1
syn region BovnarStruct13 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct14,BovnarArray1
syn region BovnarStruct14 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct15,BovnarArray1
syn region BovnarStruct15 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct16,BovnarArray1
syn region BovnarStruct16 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct17,BovnarArray1
syn region BovnarStruct17 matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct18,BovnarArray1
syn region BovnarStruct18 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct19,BovnarArray1
syn region BovnarStruct19 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct20,BovnarArray1
syn region BovnarStruct20 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct21,BovnarArray1
syn region BovnarStruct21 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct22,BovnarArray1
syn region BovnarStruct22 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct23,BovnarArray1
syn region BovnarStruct23 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct24,BovnarArray1
syn region BovnarStruct24 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct25,BovnarArray1
syn region BovnarStruct25 matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct26,BovnarArray1
syn region BovnarStruct26 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct27,BovnarArray1
syn region BovnarStruct27 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct28,BovnarArray1
syn region BovnarStruct28 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct29,BovnarArray1
syn region BovnarStruct29 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct30,BovnarArray1
syn region BovnarStruct30 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct31,BovnarArray1
syn region BovnarStruct31 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct32,BovnarArray1
syn region BovnarStruct32 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct33,BovnarArray1
syn region BovnarStruct33 matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct34,BovnarArray1
syn region BovnarStruct34 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct35,BovnarArray1
syn region BovnarStruct35 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct36,BovnarArray1
syn region BovnarStruct36 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct37,BovnarArray1
syn region BovnarStruct37 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct38,BovnarArray1
syn region BovnarStruct38 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct39,BovnarArray1
syn region BovnarStruct39 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct40,BovnarArray1
syn region BovnarStruct40 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct41,BovnarArray1
syn region BovnarStruct41 matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct42,BovnarArray1
syn region BovnarStruct42 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct43,BovnarArray1
syn region BovnarStruct43 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct44,BovnarArray1
syn region BovnarStruct44 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct45,BovnarArray1
syn region BovnarStruct45 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct46,BovnarArray1
syn region BovnarStruct46 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct47,BovnarArray1
syn region BovnarStruct47 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct48,BovnarArray1
syn region BovnarStruct48 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct49,BovnarArray1
syn region BovnarStruct49 matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct50,BovnarArray1
syn region BovnarStruct50 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct51,BovnarArray1
syn region BovnarStruct51 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct52,BovnarArray1
syn region BovnarStruct52 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct53,BovnarArray1
syn region BovnarStruct53 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct54,BovnarArray1
syn region BovnarStruct54 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct55,BovnarArray1
syn region BovnarStruct55 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct56,BovnarArray1
syn region BovnarStruct56 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct57,BovnarArray1
syn region BovnarStruct57 matchgroup=BovnarStructDelim1 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct58,BovnarArray1
syn region BovnarStruct58 matchgroup=BovnarStructDelim2 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct59,BovnarArray1
syn region BovnarStruct59 matchgroup=BovnarStructDelim3 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct60,BovnarArray1
syn region BovnarStruct60 matchgroup=BovnarStructDelim4 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct61,BovnarArray1
syn region BovnarStruct61 matchgroup=BovnarStructDelim5 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct62,BovnarArray1
syn region BovnarStruct62 matchgroup=BovnarStructDelim6 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct63,BovnarArray1
syn region BovnarStruct63 matchgroup=BovnarStructDelim7 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarStruct64,BovnarArray1
syn region BovnarStruct64 matchgroup=BovnarStructDelim8 start=/{/ end=/}/ contained contains=@BovnarContent,BovnarArray1
