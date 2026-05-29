BOVNAR — BRAND & IDENTITY PACKAGE
=================================
Unit-safe serialization for scientific & industrial systems.

CONCEPT
-------
The mark is "a value, held in its annotation." Blue brackets = Bovnar's
<type:unit> annotation, drawn as caliper jaws (measurement, precision,
a checked value). The teal faceted gem = the self-describing value,
echoing the ◈ status badge already used across the editor UI. Below
~24px the full caliper mark is replaced by a simplified chevron+gem
glyph that holds its silhouette down to 16px.


CONTENTS
--------
brand.html ........................ Brand guide. Self-contained; styled to
                                    match the existing site. Open in a browser.
                                    Deploys as brand.html alongside index.html.

logo/
  bovnar-mark.svg ................. Primary mark (transparent).
  bovnar-mark-512.png
  bovnar-mark-mono.svg ............ One-colour mark (currently white ink).
  bovnar-mark-mono-512.png
  bovnar-mark-tile.svg ............ Mark on dark rounded tile (app/social glyph).
  bovnar-mark-tile-512.png
  bovnar-logo-horizontal.svg ...... Primary lockup, dark backgrounds.
  bovnar-logo-horizontal.png
  bovnar-logo-horizontal-light.svg  Lockup for light backgrounds (dark wordmark).
  bovnar-logo-horizontal-light.png
  bovnar-logo-stacked.svg ......... Stacked lockup.
  bovnar-logo-stacked.png
  bovnar-og.svg / bovnar-og.png ... Social / Open Graph card (1200x630).

favicon/
  favicon.svg ..................... Modern favicon (transparent, theme-adaptive).
  favicon.ico ..................... Legacy multi-res (16/32/48).
  favicon-16.png / -32.png / -48.png
  apple-touch-icon.png ............ 180x180, iOS.
  icon-192.png / icon-512.png ..... PWA icons (rounded dark tile).
  icon-maskable-512.png ........... PWA maskable (extra safe-zone padding).
  site.webmanifest ................ Web app manifest.


INSTALL THE ICONS
-----------------
Place the favicon/ files at the web root, then add to <head>:

  <link rel="icon" href="/favicon.ico" sizes="any">
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <link rel="icon" type="image/png" sizes="32x32" href="/favicon-32.png">
  <link rel="apple-touch-icon" href="/apple-touch-icon.png">
  <link rel="manifest" href="/site.webmanifest">


COLOUR TOKENS
-------------
Editor canvas   #1E1E1E   --editor-bg
Sidebar/raised  #252526   --sidebar-bg
Panel/tabs      #2D2D2D   --panel-bg
Border          #3E3E42   --border
Primary blue    #007ACC   --blue2   (action / status / statusbar)
Bracket blue    #569CD6   --blue    (links, mark gradient top)
Gem teal        #4EC9B0   --teal    (units / confirmation)
Gem highlight   #9CDCFE   --cyan    (accent / gem facet)
Text            #EDEDED   --text
Text secondary  #C2C2C2   --text2
Text muted      #A0A0A0   --text3   (the ".bvnr" extension)
Error           #F44747   --red

Mark gem gradient: #9CDCFE -> #4EC9B0 -> #33A78F (facets: #2C9281, #3FB89E).
Mark bracket gradient: #6CB0EA (top) -> #0A85D6 (bottom).


TYPOGRAPHY
----------
IBM Plex Sans .... Interface, prose, headings (300/400/500/600 + italic).
JetBrains Mono ... Code, units, UI chrome, and the wordmark.
                   Wordmark: JetBrains Mono Medium, tracking +0.085em,
                   ".bvnr" set in Regular, muted grey (--text3).


CLEAR SPACE & MINIMUM SIZE
--------------------------
Clear space = the width of one gem (◆) on every side.
Minimum: full mark 24px; favicon glyph 16px.


VOICE
-----
Precise, plainspoken, unhyped. State the failure mode plainly (the wrong
unit is the bug); show the syntax; speak to engineers. Avoid hype words,
emoji, and any claim of unit conversion (Bovnar validates units; the
application converts).

Taglines:
  Primary   — Unit-safe serialization for scientific & industrial systems.
  Short     — Every value carries its unit.
  Technical — Self-describing, dimensionally trusted, no external schema.


REGENERATING
------------
The vector sources are generated from Python (fontTools + cairosvg):
gen_logo.py, gen_favicon.py, gen_og.py, with textpath.py and the
JetBrains Mono TTFs. Wordmarks are flattened to outlines so the SVGs
need no installed fonts.
