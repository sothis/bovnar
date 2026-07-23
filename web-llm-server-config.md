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

1. **`text/markdown` MIME type** for `.md` files (RFC 7763) — so `/index.md` and
   `/doc/*.md` are served as Markdown, not `text/plain` or a download.
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
  `location = /` declares its own `add_header`, the server-level
  `Strict-Transport-Security` stops inheriting *there* — so HSTS is repeated
  inside that location.

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

# Canonical host (only the additions inside are new; TLS/redirect blocks unchanged).
server {
    listen 443 ssl;
    listen [::]:443 ssl;
    server_name www.bovnar.io;

    root /var/www/html;
    index index.html index.htm index.nginx-debian.html;

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

        add_header Strict-Transport-Security "max-age=31536000" always;

        try_files /index.html =404;
    }

    # German home page: the same, pointing at /de/index.md.
    location = /de/ {
        if ($bvnr_wants_md) { return 302 /de/index.md; }
        add_header Link '</de/index.md>; rel="alternate"; type="text/markdown"' always;
        add_header Vary 'Accept' always;
        add_header Strict-Transport-Security "max-age=31536000" always;
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
# 1. Serve .md as Markdown (RFC 7763).
AddType text/markdown .md

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
