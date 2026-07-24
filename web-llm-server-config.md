# Web-server configuration for LLM visibility

Most of the "make the site visible to LLMs" work ships with the static site
(`web/`): `robots.txt` with a `Content-Signal:` directive, `/llms.txt`,
`/llms-full.txt`, `/index.md` and the per-doc `/doc/*.md` Markdown, the
`<link rel="alternate" type="text/markdown">` in the page head, and an
off-screen Markdown-URL hint. Those deploy via `publish_web.sh` and need nothing
on the server.

Three techniques are **protocol-level** and therefore live in the web-server
config (they cannot ride the `rsync` of `web/` into the doc root). `www.bovnar.io`
runs **nginx** — the tailored block for the live config is below; an Apache
equivalent follows as a portable alternative. All three are additive and safe:
they only add response headers and an `Accept`-based redirect for the home page.

0. **HTTP/2** (`http2 on;`) — ALPN advertised only `http/1.1` before, so the page
   and its subresources shared one connection pool with the usual six-connection
   cap. Clients that do not speak h2 still negotiate 1.1.
1. **`text/markdown` MIME type** for `.md` files (RFC 7763) — so `/index.md` and
   `/doc/*.md` are served as Markdown, not `text/plain` or a download. `.ebnf`
   (the single non-Markdown document) gets `text/plain` for the same reason.
2. **`Link: …; rel="alternate"; type="text/markdown"`** response header on the
   home page, plus **`Vary: Accept`** — protocol-level discovery of the Markdown
   edition before the HTML body is read.
3. **`Accept: text/markdown` content negotiation** — when a client (Claude Code,
   Cursor, …) prefers Markdown, serve `/index.md` for `/`.

Validate afterwards with <https://acceptmarkdown.com> and
<https://isitagentready.com>.

---

## nginx (the live config)

`www.bovnar.io` runs nginx. Only the canonical-host `server{}` block serves the
site, so that is the only block that changes; the apex/`.com`/`.net`/`.org`
redirect blocks stay as they are. Two nginx specifics are handled below:

- **A `map` must live in the `http{}` context** — so it goes at the top of the
  site config file, *above* the `server` blocks (a site file under
  `sites-enabled/` is included inside `http{}`).
- **`add_header` replaces inherited headers at a new level.** The moment
  `location = /` declares its own `add_header`, the server-level headers stop
  inheriting *there* — so every such location `include`s
  `snippets/bovnar-headers.conf` (documented below) instead of restating them
  one at a time and losing one of them the next time the set grows.

```nginx
# AI/LLM content negotiation: does the client prefer Markdown? (technique 3)
# Browsers send "text/html,…" with no text/markdown token and never match, so
# they are unaffected. AI tools (Claude Code, Cursor, …) send
# "Accept: text/markdown[, text/html;q=…]" and match. Pragmatic token test, not a
# full q-value parse — fine because such clients list Markdown as their primary
# type. MUST be in the http{} context, hence above the server blocks.
map $http_accept $bvnr_wants_md {
    default            0;
    "~*text/markdown"  1;
}

# Cache policy. Every versioned asset on the page is requested as
# <path>?v=<content-hash> (wasm/build_wasm.sh stamps them from the file's own
# bytes), so a fingerprinted URL can safely be cached forever -- that is the
# entire point of the fingerprint, and the server was not telling anyone. A
# request for the same path WITHOUT ?v= is not necessarily the same bytes
# forever, so it gets a short TTL and revalidates instead. HTML is left alone:
# it carries an ETag and must reflect a publish immediately.
#
# Only a 12-hex-char stamp qualifies, because that is the shape build_wasm.sh
# emits and cmake/cache_stamps.cmake verifies. A hand-written stamp gets the
# short TTL rather than a year: fonts/fonts.css shipped as ?v=20260724 for
# months, correct only while someone remembered to bump it.
map $arg_v $bvnr_cache_ctl {
    default            "public, max-age=3600";
    "~^[0-9a-f]{12}$"  "public, max-age=31536000, immutable";
}

# Canonical host (only the additions inside are new; TLS/redirect blocks unchanged).
server {
    listen 443 ssl;
    listen [::]:443 ssl;
    server_name www.bovnar.io;

    root /var/www/html;
    index index.html index.htm index.nginx-debian.html;

    # ── Compression ──────────────────────────────────────────────────────────
    # nginx compresses text/html and nothing else unless told otherwise, so the
    # site shipped 791 KB per cold visit that did not need to move: llms-full.txt
    # 560->185 KB, bovnar_wasm_core.js 252->92 KB, highlight.js 122->41 KB, the
    # spec .md 115->38 KB. Measured against the live server; HTML was already
    # gzipped (376->129 KB) because that one is nginx's built-in default.
    # text/html is deliberately absent below: listing it is a duplicate and nginx
    # warns.
    #
    # gzip_vary is not cosmetic. The live server already gzips HTML while sending
    # only "Vary: Accept" (from the home page's add_header), never
    # "Vary: Accept-Encoding" -- so a shared cache may hand the compressed body
    # to a client that never asked for it. nginx appends Accept-Encoding to Vary
    # itself once this is on.
    gzip              on;
    gzip_vary         on;
    gzip_comp_level   6;
    gzip_min_length   256;
    gzip_proxied      any;
    gzip_types        text/plain text/css text/xml text/markdown
                      application/javascript application/json
                      application/manifest+json application/xml
                      application/rss+xml image/svg+xml;

    # Branded 404 page (web/404.html). The =404 in try_files below triggers it.
    error_page 404 /404.html;
    location = /404.html { internal; }

    # Home page: advertise + serve the Markdown edition to AI tools.
    # Declares add_header, so it must repeat the server-level HSTS (see above).
    location = / {
        # 3. Content negotiation: 302 Markdown-preferring agents to /index.md.
        #    302 (not an internal rewrite) keeps HTML and Markdown at distinct
        #    URLs so caches key correctly on "Vary: Accept".
        if ($bvnr_wants_md) { return 302 /index.md; }

        # 2. Protocol-level discovery of the Markdown alternate + the Vary.
        add_header Link '</index.md>; rel="alternate"; type="text/markdown"' always;
        add_header Vary 'Accept' always;

        include snippets/bovnar-headers.conf;

        try_files /index.html =404;
    }

    # German home page: the same, pointing at /de/index.md.
    location = /de/ {
        if ($bvnr_wants_md) { return 302 /de/index.md; }
        add_header Link '</de/index.md>; rel="alternate"; type="text/markdown"' always;
        add_header Vary 'Accept' always;
        include snippets/bovnar-headers.conf;
        try_files /de/index.html =404;
    }

    # 1. Serve .md as Markdown (RFC 7763) with a UTF-8 charset. Empty types{}
    #    drops any inherited extension map so default_type wins; charset_types
    #    adds text/markdown to the set that gets "; charset=…" appended.
    location ~ \.md$ {
        types         { }
        default_type  text/markdown;
        charset       utf-8;
        charset_types text/markdown;
        try_files $uri =404;
    }

    # The one non-Markdown document. Nothing maps .ebnf, so it fell through to
    # nginx's default_type (application/octet-stream) and the browser offered a
    # download instead of showing the grammar -- while its nine Markdown siblings
    # on /docs/ displayed inline, and /docs/grammar/ advertises it as
    # <link rel="alternate" type="text/plain">, which the server contradicted.
    location ~ \.ebnf$ {
        types         { }
        default_type  text/plain;
        charset       utf-8;
        charset_types text/plain;
        try_files $uri =404;
    }

    # site.webmanifest: nginx's mime.types has no .webmanifest entry, so it went
    # out as application/octet-stream. Browsers are lenient about it, but the
    # spec type is application/manifest+json -- and naming it also brings the
    # file under gzip_types above.
    location ~ \.webmanifest$ {
        types         { }
        default_type  application/manifest+json;
        charset       utf-8;
        try_files $uri =404;
    }

    # Static assets only -- see the $bvnr_cache_ctl map. Declaring add_header
    # here stops this location inheriting the server-level headers, so it
    # includes them (the same trap as the home-page locations above). pdf and zip
    # are in the list because the documentation ships as both.
    location ~* \.(js|css|woff2|jpe?g|png|svg|ico|pdf|zip)$ {
        add_header Cache-Control "$bvnr_cache_ctl" always;
        include snippets/bovnar-headers.conf;
        try_files $uri =404;
    }

    location / {
        try_files $uri $uri/ =404;
    }

    # … existing TLS block, ssl_stapling, and server-level HSTS unchanged …
}
```

Reload with `nginx -t && systemctl reload nginx`.

Location precedence works out: `= /` (exact) handles the home page, `~ \.md$`
(regex) handles `.md` files — including the `/index.md` redirect target — and the
prefix `location /` handles everything else unchanged. `/llms.txt` and
`/llms-full.txt` stay `text/plain` (they are `.txt`); to serve them as Markdown
too, widen the regex to `~ (\.md$|^/llms(-full)?\.txt$)`.

---

## Apache

Requires `mod_headers`, `mod_mime`, and `mod_rewrite` (all standard). Put this in
the site's `<VirtualHost>` / `<Directory>` block, or in a `.htaccess` at the doc
root **only if `AllowOverride` already permits `FileInfo Indexes`** — otherwise
Apache returns 500. Prefer the vhost.

```apache
# 1. Serve .md as Markdown (RFC 7763), and the one non-Markdown document as
#    plain text -- unmapped, .ebnf is offered as a download rather than shown.
AddType text/markdown .md
AddType text/plain .ebnf
AddCharset UTF-8 .md .ebnf

# 2. Advertise the Markdown alternate + vary on the home page.
<If "%{REQUEST_URI} == '/'">
    Header set Link "</index.md>; rel=\"alternate\"; type=\"text/markdown\""
    Header append Vary "Accept"
</If>

# 3. Content negotiation for the home page: serve /index.md to Markdown-preferring
#    clients (302 so caches key on Vary: Accept rather than pinning the HTML).
RewriteEngine On
RewriteCond %{HTTP_ACCEPT} text/markdown [NC]
RewriteRule ^$ /index.md [R=302,L]
```

---

## Notes

- The `302` (not an internal rewrite) keeps HTML and Markdown at distinct URLs, so
  intermediary caches key correctly on `Vary: Accept`.
- `robots.txt` already carries `Content-Signal: search=yes, ai-input=yes,
  ai-train=yes`. Change `ai-train=yes` to `ai-train=no` if you do **not** want the
  content used for model training; the other two signals keep search and live-AI
  context enabled.
- Nothing here changes the human-facing site: browsers send
  `Accept: text/html,…` and continue to receive `index.html`.
- The `$bvnr_cache_ctl` map only ever sees a stamp the *page* asked for, so an
  asset reached from somewhere else never earned the immutable year: the woff2
  files are named only inside `fonts/fonts.css`, which stamped none of them, and
  ~440 kB of fonts that never change revalidated hourly on the critical path.
  `./gen_font_stamps.py` now derives those stamps (and the pages' stamp for
  `fonts.css` itself) from the bytes, gated by the `bvnr_font_stamps` test — the
  same contract `cmake/cache_stamps.cmake` gives the WASM artifacts.

## Optional: consolidate the raw `.md` to the HTML docs for search

The same documentation is served three ways — the HTML pages `/docs/<slug>/` (the
canonical, indexable form), the raw Markdown `/doc/*.md` (for LLM tools), and the
PDFs. To keep search engines from treating the raw `.md` as competing pages while
LLM tools keep fetching it, advertise the HTML page as canonical via a response
header on the `.md`. This is a search-only signal; it does not affect what LLM
crawlers receive.

The map holds the **complete** `Link` header value (RFC 8288: the URI in angle
brackets) for each doc `.md`, and an empty string for everything else. Add it to
the `http{}` context beside the other `map`:

```nginx
map $uri $bvnr_canon_hdr {
    default                            "";
    "/doc/0_bovnar_tutorial.md"        "<https://www.bovnar.io/docs/tutorial/>; rel=\"canonical\"";
    "/doc/1_bovnar_spec.md"            "<https://www.bovnar.io/docs/spec/>; rel=\"canonical\"";
    "/doc/2_bovnar_unit_system.md"     "<https://www.bovnar.io/docs/units/>; rel=\"canonical\"";
    "/doc/3_bovnar_readwrite_api.md"   "<https://www.bovnar.io/docs/api/>; rel=\"canonical\"";
    "/doc/4_bovnar_python_bindings.md" "<https://www.bovnar.io/docs/python/>; rel=\"canonical\"";
    "/doc/6_bovnar_faq.md"             "<https://www.bovnar.io/docs/faq/>; rel=\"canonical\"";
    "/doc/7_bovnar_conformance.md"     "<https://www.bovnar.io/docs/conformance/>; rel=\"canonical\"";
    "/doc/8_unit_cheatsheet.md"        "<https://www.bovnar.io/docs/cheatsheet/>; rel=\"canonical\"";
    "/doc/9_bovnar_streaming.md"       "<https://www.bovnar.io/docs/streaming/>; rel=\"canonical\"";
}
```

Then, inside the existing `location ~ \.md$`, add the header — nginx omits an
`add_header` whose value evaluates to the empty string, so `/index.md` and
`/de/index.md` get no canonical header, while the doc `.md` do:

```nginx
    # (add to the existing .md location; also include the shared header snippet
    #  there, since declaring add_header stops the location inheriting it)
    add_header Link "$bvnr_canon_hdr" always;
    include snippets/bovnar-headers.conf;
```

`/doc/5_bovnar.ebnf` isn't matched by the `.md` location, and is deliberately left
out of the canonical map above — it is niche enough not to compete for the
`/docs/grammar/` ranking. It does still need its own `location` for the MIME type
(above), or it downloads instead of displaying. The complete assembled config is
in the next section.

## Response headers (`/etc/nginx/snippets/bovnar-headers.conf`)

The pages carry their own hash-based CSP in a `<meta>`, which covers scripts,
styles and everything else that loads — but `frame-ancestors` is ignored in a
`<meta>` by specification, so until this snippet existed the site could be framed
by anyone. That directive only works as a header, so it is one here, alongside
the two headers no static site should be without.

Kept in one file because `add_header` is not additive: a location that declares
any header of its own stops inheriting the entire server-level set. Every
location in the config below that adds a header therefore `include`s this file,
which is what stops the set from quietly thinning out one location at a time.

```nginx
##
# bovnar — the header set every response from the canonical host carries.
#
# add_header is NOT additive across configuration levels: the moment a location
# declares one header of its own, it stops inheriting the entire set from the
# server block. That trap already cost this config its HSTS in four locations
# once. Keeping the set in one file and including it wherever headers are
# declared means the next location cannot silently ship a thinner set.
#
#   Strict-Transport-Security  — HTTPS-only for a year.
#   X-Content-Type-Options     — no MIME sniffing; every type here is declared.
#   Referrer-Policy            — send the full URL same-origin, the bare origin
#                                cross-origin, nothing when downgrading.
#   Content-Security-Policy    — framing only. The pages carry their own full
#                                policy in a <meta>, where frame-ancestors is
#                                ignored by definition, so the site was
#                                embeddable in anyone's iframe. A header is the
#                                only place that directive works; the two
#                                policies are enforced together, and this one
#                                restricts nothing else.
##
add_header Strict-Transport-Security "max-age=31536000" always;
add_header X-Content-Type-Options "nosniff" always;
add_header Referrer-Policy "strict-origin-when-cross-origin" always;
add_header Content-Security-Policy "frame-ancestors 'none'" always;
```

## Complete assembled nginx config

The live `www.bovnar.io` config — this block is a verbatim copy of
`/etc/nginx/sites-available/bovnar` as applied, with every AI/SEO, MIME,
compression and caching addition folded in (the two apex/`.com`/`.net`/`.org`
redirect blocks are unchanged from the original):

```nginx
##
# bovnar — canonical host: www.bovnar.io
# Every other name (apex bovnar.io + all .com/.net/.org) redirects here.
##

# AI/LLM content negotiation: does the client prefer Markdown? (http{} context)
map $http_accept $bvnr_wants_md {
    default            0;
    "~*text/markdown"  1;
}

# Cache lifetimes. Every versioned asset is requested as <path>?v=<content-hash>
# (wasm/build_wasm.sh stamps them from the file's own bytes), so a fingerprinted
# URL is safe to cache forever -- that is the point of the fingerprint. A bare
# path is not necessarily the same bytes forever, so it revalidates sooner.
# Only a 12-hex-char stamp earns the immutable year, because that is what
# build_wasm.sh emits (sha256 of the file, cut -c1-12) and it is verified by
# cmake/cache_stamps.cmake. Anything else -- a hand-written date, a hand-edited
# value -- falls to the short TTL instead of pinning a stale asset in caches for
# a year. fonts/fonts.css was exactly that case: ?v=20260724, kept correct only
# by whoever remembered to bump it. It is content-hashed now, and this pattern
# means the next one to slip through is merely uncached, not wrong.
map $arg_v $bvnr_cache_ctl {
    default            "public, max-age=3600";
    "~^[0-9a-f]{12}$"  "public, max-age=31536000, immutable";
}

# Search consolidation: the full "Link: rel=canonical" value (RFC 8288 — URI in
# angle brackets) for each raw doc .md; empty for /index.md and /de/index.md, so
# nginx adds no canonical header there (it omits an empty-valued add_header).
map $uri $bvnr_canon_hdr {
    default                            "";
    "/doc/0_bovnar_tutorial.md"        "<https://www.bovnar.io/docs/tutorial/>; rel=\"canonical\"";
    "/doc/1_bovnar_spec.md"            "<https://www.bovnar.io/docs/spec/>; rel=\"canonical\"";
    "/doc/2_bovnar_unit_system.md"     "<https://www.bovnar.io/docs/units/>; rel=\"canonical\"";
    "/doc/3_bovnar_readwrite_api.md"   "<https://www.bovnar.io/docs/api/>; rel=\"canonical\"";
    "/doc/4_bovnar_python_bindings.md" "<https://www.bovnar.io/docs/python/>; rel=\"canonical\"";
    "/doc/6_bovnar_faq.md"             "<https://www.bovnar.io/docs/faq/>; rel=\"canonical\"";
    "/doc/7_bovnar_conformance.md"     "<https://www.bovnar.io/docs/conformance/>; rel=\"canonical\"";
    "/doc/8_unit_cheatsheet.md"        "<https://www.bovnar.io/docs/cheatsheet/>; rel=\"canonical\"";
    "/doc/9_bovnar_streaming.md"       "<https://www.bovnar.io/docs/streaming/>; rel=\"canonical\"";
}

# 1) Serve the site over HTTPS on the canonical host only
server {
    listen 443 ssl;
    listen [::]:443 ssl;
    # ALPN offered only http/1.1 before this, so every visit fetched the page and
    # its 15 subresources over HTTP/1.1 with the usual 6-connection cap and
    # head-of-line blocking. Directive form, not "listen ... http2", which nginx
    # deprecated in 1.25.1. Clients that do not speak h2 still negotiate 1.1.
    http2 on;
    server_name www.bovnar.io;

    root /var/www/html;
    index index.html index.htm index.nginx-debian.html;

    # Compression. nginx compresses text/html and nothing else unless told, and
    # the http{} block leaves gzip_types commented out -- so llms-full.txt (560
    # KB), bovnar_wasm_core.js (252 KB), highlight.js (122 KB) and the doc .md
    # all went out raw: 791 KB per cold visit, 58% of the text bytes on the wire.
    # text/html is deliberately absent: listing it is a duplicate and nginx warns.
    # gzip_vary is required for correctness, not tuning -- without it a gzipped
    # response carries no "Vary: Accept-Encoding" and a shared cache may hand the
    # compressed body to a client that never asked for it.
    gzip              on;
    gzip_vary         on;
    gzip_comp_level   6;
    gzip_min_length   256;
    gzip_proxied      any;
    gzip_types        text/plain text/css text/xml text/markdown
                      application/javascript application/json
                      application/manifest+json application/xml
                      application/rss+xml image/svg+xml;

    # Label the charset instead of leaving it to the client's locale. llms.txt
    # and llms-full.txt are text/plain and carry 19 kB of non-ASCII (µ, Ω, ×, —)
    # with no in-band declaration to fall back on, so a browser guessing
    # windows-1252 rendered them as mojibake. The .md and .ebnf locations each
    # said utf-8 already; saying it once at the top covers text/html, text/plain
    # and text/xml (nginx's default charset_types) and cannot drift per location.
    # No source_charset is set, so nginx labels the bytes, it does not re-code
    # them.
    charset utf-8;
    # nginx's default charset_types leaves out text/css and the manifest type,
    # and both files here contain non-ASCII. A stylesheet with no charset falls
    # back to the encoding of the document that linked it, which is UTF-8 today
    # but is not something a stylesheet should have to depend on.
    # text/html is deliberately absent: charset_types always includes it and
    # naming it again makes nginx warn on every config test and reload (the same
    # reason gzip_types above leaves it out).
    charset_types text/xml text/plain text/css
                  application/javascript application/manifest+json;

    # Branded 404 page (web/404.html).
    error_page 404 /404.html;
    location = /404.html { internal; }

    # Home page: advertise + serve the Markdown edition to AI tools. Declaring
    # add_header here drops the whole inherited set, so the snippet is included.
    location = / {
        if ($bvnr_wants_md) { return 302 /index.md; }
        add_header Link '</index.md>; rel="alternate"; type="text/markdown"' always;
        add_header Vary 'Accept' always;
        include snippets/bovnar-headers.conf;
        try_files /index.html =404;
    }

    # German home page, pointing at /de/index.md.
    location = /de/ {
        if ($bvnr_wants_md) { return 302 /de/index.md; }
        add_header Link '</de/index.md>; rel="alternate"; type="text/markdown"' always;
        add_header Vary 'Accept' always;
        include snippets/bovnar-headers.conf;
        try_files /de/index.html =404;
    }

    # .md as Markdown (RFC 7763) + UTF-8; plus the canonical->HTML header for the
    # doc .md (empty, hence omitted, for /index.md and /de/index.md).
    location ~ \.md$ {
        types         { }
        default_type  text/markdown;
        charset       utf-8;
        charset_types text/markdown;
        add_header Link "$bvnr_canon_hdr" always;
        include snippets/bovnar-headers.conf;
        try_files $uri =404;
    }

    # The one non-Markdown document. Nothing maps .ebnf, so it fell through to
    # default_type (application/octet-stream) and the browser offered a download
    # instead of showing the grammar -- while its nine Markdown siblings on
    # /docs/ display inline, and /docs/grammar/ advertises it as
    # <link rel="alternate" type="text/plain">, which the server contradicted.
    location ~ \.ebnf$ {
        types         { }
        default_type  text/plain;
        charset       utf-8;
        charset_types text/plain;
        try_files $uri =404;
    }

    # mime.types has no .webmanifest entry, so the manifest went out as
    # application/octet-stream. Naming it also brings it under gzip_types.
    location ~ \.webmanifest$ {
        types         { }
        default_type  application/manifest+json;
        charset       utf-8;
        try_files $uri =404;
    }

    # Static assets -- see the $bvnr_cache_ctl map. Nothing sent Cache-Control at
    # all before this, so every asset revalidated on every visit. Declaring
    # add_header here stops this location inheriting the server-level headers, so
    # it includes them (same trap as the home-page locations above).
    #
    # pdf and zip are here because the documentation ships as both: ten PDFs and
    # a 2.5 MB bundle, none of which matched this list, so each download
    # revalidated on every visit while every other asset on the site was told
    # what to do. They are regenerated on publish, hence the map's short default
    # rather than a year -- a stale PDF for an hour, not for a year.
    location ~* \.(js|css|woff2|jpe?g|png|svg|ico|pdf|zip)$ {
        add_header Cache-Control "$bvnr_cache_ctl" always;
        include snippets/bovnar-headers.conf;
        try_files $uri =404;
    }

    location / {
        try_files $uri $uri/ =404;
    }

    # TLS — the multi-SAN cert lineage is named "bovnar.com"; do NOT repoint at
    # /etc/letsencrypt/live/bovnar.io/ (no such path).
    ssl_certificate         /etc/letsencrypt/live/bovnar.com/fullchain.pem;
    ssl_certificate_key     /etc/letsencrypt/live/bovnar.com/privkey.pem;
    ssl_trusted_certificate /etc/letsencrypt/live/bovnar.com/chain.pem;
    include                 /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam             /etc/letsencrypt/ssl-dhparams.pem;

    # ssl_stapling removed: Let's Encrypt has retired OCSP and its certificates
    # carry no responder URL, so nginx logged "ssl_stapling ignored" on every
    # config test and reload while stapling nothing. A standing benign warning
    # is how a real one gets missed.

    include snippets/bovnar-headers.conf;
}

# 2) HTTPS redirect for every non-canonical name -> https://www.bovnar.io
server {
    listen 443 ssl default_server;
    listen [::]:443 ssl default_server;
    http2 on;
    server_name bovnar.io
                bovnar.com www.bovnar.com
                bovnar.net www.bovnar.net
                bovnar.org www.bovnar.org;

    ssl_certificate     /etc/letsencrypt/live/bovnar.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/bovnar.com/privkey.pem;
    include             /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam         /etc/letsencrypt/ssl-dhparams.pem;

    include snippets/bovnar-headers.conf;

    return 301 https://www.bovnar.io$request_uri;
}

# 3) HTTP redirect for every name (incl. www.bovnar.io) -> https://www.bovnar.io
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    server_name bovnar.io www.bovnar.io
                bovnar.com www.bovnar.com
                bovnar.net www.bovnar.net
                bovnar.org www.bovnar.org;

    return 301 https://www.bovnar.io$request_uri;
}
```

Apply with `nginx -t && systemctl reload nginx`, then validate with
acceptmarkdown.com / isitagentready.com.

## Analytics (optional, operational)

To see whether any of this pays off, log requests to the AI endpoints and
distinguish AI traffic — no repo change, just your existing access log:

- Watch the paths `/llms.txt`, `/llms-full.txt`, `/index.md`, and `/doc/*.md`.
- Bucket by `User-Agent` to spot AI crawlers (`GPTBot`, `ClaudeBot`,
  `PerplexityBot`, `Google-Extended`, …) and by `Accept: text/markdown`.
- Bucket by `Referer` host to catch human-initiated fetches from `chatgpt.com`,
  `claude.ai`, `perplexity.ai`.

Example (nginx combined log):

```sh
awk '$7 ~ /\.md$|llms(-full)?\.txt/' /var/log/nginx/access.log \
  | grep -iE 'GPTBot|ClaudeBot|PerplexityBot|Google-Extended|text/markdown'
```
