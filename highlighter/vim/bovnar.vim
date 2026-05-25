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
hi BovnarStructDelim  ctermfg=66  guifg=#6e7a86 gui=none
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

syn match   BovnarOctetData    '\\x[0-9A-Fa-f]\{2\}'

syn match   BovnarSpecialFloat '\$\(nan\|-infinity\|infinity\)\$'

syn match   BovnarArrayDelim1  '\[\@<!\['
syn match   BovnarArrayDelim1  '\]\@<!\]'
syn match   BovnarArrayDelim2  '\[\@<=\['
syn match   BovnarArrayDelim2  '\]\@<=\]'
syn match   BovnarArrayDelim3  '\[\[\@<=\['
syn match   BovnarArrayDelim3  '\]\]\@<=\]'
syn match   BovnarArrayDelim4  '\[\[\[\@<=\['
syn match   BovnarArrayDelim4  '\]\]\]\@<=\]'
syn match   BovnarArrayDelim5  '\[\[\[\[\@<=\['
syn match   BovnarArrayDelim5  '\]\]\]\]\@<=\]'

syn region  BovnarComment start=/#/ end=/$/

syn match   BovnarArraySep    ','
syn match   BovnarRowSep      '/'
syn match   BovnarStructDelim '[{}]'

syn match   BovnarKeySigil '\.'

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

syn match   BovnarTypeUnit  '\<[A-Za-zµΩ°][A-Za-z0-9_·\-\+\/\^]*\>'
syn match   BovnarUnitPrefix '[A-Za-zµ][A-Za-z0-9]*\~[A-Za-z0-9_·\-\+\/*\^]*'

syn match   BovnarUnitSep   '[*\/]'
syn match   BovnarUnitExp   '[⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻]'
syn match   BovnarUnitExp   '\^[+-]\=\d\+'

syn match   BovnarType     '\v(float_fix|float_dec|float|uint|sint|utf8)'
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
