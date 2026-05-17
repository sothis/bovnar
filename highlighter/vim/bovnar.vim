" Bovnar syntax file - Cyberpunk edition
" Neon palette: magenta, cyan, yellow, green, orange
" All patterns identical to bovnar.vim, only colors changed.

syn clear

" Highlight groups (Cyberpunk neon palette)
hi BovnarComment     ctermfg=244 guifg=#4a4e69   gui=none
hi BovnarKeySigil    ctermfg=213 guifg=#c695dc
hi BovnarKeyName     ctermfg=39  guifg=#00e0ff   gui=none
hi BovnarSemicolon   ctermfg=240 guifg=#3e4462   gui=none
hi BovnarAssign      ctermfg=200 guifg=#ff6a8c   gui=none
hi BovnarType        ctermfg=200 guifg=#ff2a7d   gui=bold
hi BovnarTypeDelim   ctermfg=38  guifg=#00b4d8   gui=none
hi BovnarTypeSep     ctermfg=240 guifg=#3e4462   gui=none
hi BovnarTypeParam   ctermfg=214 guifg=#ffa500   gui=none
hi BovnarNoUnit      ctermfg=46  guifg=#00ff80   gui=none
hi BovnarTypeUnit    ctermfg=46  guifg=#00ff80   gui=none
hi BovnarSpecialFloat ctermfg=212 guifg=#c084fc
hi BovnarFloat       ctermfg=228 guifg=#ffe700   gui=none
hi BovnarInteger     ctermfg=228 guifg=#ffe700   gui=none
hi BovnarString      ctermfg=46  guifg=#00ff80   gui=none
hi BovnarEscape      ctermfg=46  guifg=#00ff80   gui=bold
hi BovnarInvalidEscape ctermfg=203 guifg=#ff4060 gui=undercurl
hi BovnarSymbol      ctermfg=39  guifg=#00e0ff   gui=none
hi BovnarRefOp       ctermfg=200 guifg=#ff2a7d   gui=bold
hi BovnarRefPath     ctermfg=214 guifg=#ffa500   gui=none
hi BovnarArrayDelim  ctermfg=38  guifg=#00b4d8   gui=none
hi BovnarArraySep    ctermfg=240 guifg=#3e4462   gui=none
hi BovnarRowSep      ctermfg=240 guifg=#3e4462   gui=none
hi BovnarStructDelim ctermfg=38  guifg=#00b4d8   gui=none
hi BovnarUnitSep     ctermfg=240 guifg=#3e4462   gui=none
hi BovnarInlineUnit  ctermfg=46  guifg=#00ff80   gui=none

" Patterns
" NOTE: LAST defined wins for overlapping matches at same position.

" Comments
syn region  BovnarComment start=/#/ end=/$/

" Type keywords
syn match   BovnarType '\v(float_fix|float_dec|float|uint|sint|utf8)'
syn match   BovnarNoUnit 'no_unit'
syn match   BovnarTypeParam '\vq\d+'
syn match   BovnarTypeParam '\v_[1-9]\d*'
syn match   BovnarTypeDelim '[<>]'
syn match   BovnarTypeSep ':'
syn match   BovnarTypeSep ','

syn match   BovnarTypeUnit contained
      \ '\v<[A-Za-z0-9_:,~^\sµΩ°\-/·*\+\-]+\s*>$'

syn region  BovnarTypeAnn
      \ start=/</ end=/>/
      \ contains=BovnarType,BovnarTypeParam,BovnarTypeUnit,
      \          BovnarNoUnit,BovnarTypeDelim,BovnarTypeSep,
      \          BovnarSpecialFloat,BovnarComment
      \ keepend

" Operators
syn match   BovnarAssign '='
syn match   BovnarSemicolon ';'

" Keys
syn match   BovnarKeySigil '\.'
syn match   BovnarKeyName '\.\([A-Za-z_][A-Za-z0-9_+\-]*\)'

" Strings
syn region  BovnarString
      \ start=/"/ skip=/\\\\\|\\./ end=/"/
      \ contains=BovnarEscape,BovnarInvalidEscape
syn match   BovnarEscape '\\[tnvfr"\\]'
syn match   BovnarInvalidEscape '\\\\.'

" Arrays and structs
syn match   BovnarArrayDelim '[\[\]]'
syn match   BovnarArraySep ','
syn match   BovnarRowSep '/'
syn match   BovnarStructDelim '[{}]'

" Reference operator - bare & (AFTER RefPath so only unmatched & remains)
syn match   BovnarRefOp '&'

" Reference path - &.segment (AFTER RefOp so it wins for &.hostname)
syn match   BovnarRefPath '&\.[A-Za-z_][A-Za-z0-9_+\-]*'

" Symbols - bare identifier catch-all
syn match   BovnarSymbol '^\s*\zs[A-Za-z_][A-Za-z0-9_+\-]*'

" Special floats - $nan$, $infinity$, $-infinity$
syn match   BovnarSpecialFloat '\$\(nan\|-infinity\|infinity\)\$'

" Integers (BEFORE floats so floats win for overlapping matches)
syn match   BovnarInteger '\v\d+'

" Floats - exponent-only forms (e.g. 1e6)
syn match   BovnarFloat '\v\d+[eE][+-]?\d+'

" Floats - dot-separated forms (e.g. 3.14, 123., .50) - LAST float
syn match   BovnarFloat '\v(\.\d+|\d+\.\d+|\d+\.)'

" Inline units - AFTER Float so floats/ints win for bare numbers
syn match   BovnarInlineUnit /\v\s+[A-Za-zµ°][A-Za-z0-9_.\-/:^*~µΩ°·¹²³⁴⁵⁶⁷⁸⁹ⁿ⁺⁻]*/
