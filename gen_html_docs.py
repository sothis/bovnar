#!/usr/bin/env python3
"""Render the Bovnar documentation as standalone, SEO-optimized HTML pages.

The canonical docs are Markdown (doc/*.md). They are already published as raw
Markdown (/doc/*.md, great for LLMs) and as PDFs (/doc/pdf/*.pdf), but neither
gives a search engine an indexable HTML page with its own <title>, meta
description, canonical URL, and heading structure. This generator produces one
crawlable page per doc under web/docs/<slug>/index.html, plus a /docs/ index.

Design notes:
  * Inter-doc links (e.g. "6_bovnar_faq.md#foo") are rewritten to the HTML URL
    "/docs/faq/#foo".
  * Heading ids use a GitHub-style slugify so the docs' existing intra-page
    "#anchor" links resolve (the same slugs the raw Markdown assumes).
  * The FAQ page carries FAQPage structured data built from its Q&A headings.
  * Pages are self-contained (inline CSS, no executable inline scripts) with a
    locked-down CSP, so they need no gen_csp restamping.

publish_web.sh runs this before staging. Run by hand: python3 gen_html_docs.py
"""
import datetime
import html
import os
import re
import subprocess
import sys

import markdown
from pygments.formatters import HtmlFormatter

ROOT = os.path.dirname(os.path.abspath(__file__))


def git_date(relpath, first=False):
    """Last- (or, first=True, first-) commit date YYYY-MM-DD of a repo file;
    falls back to the file mtime, then today."""
    args = ["git", "-C", ROOT, "log", "--format=%cs", "--", relpath]
    if first:
        args = ["git", "-C", ROOT, "log", "--diff-filter=A", "--follow",
                "--format=%cs", "--", relpath]
    try:
        out = subprocess.run(args, capture_output=True, text=True,
                             check=True).stdout.strip().splitlines()
        if out:
            return out[-1] if first else out[0]
    except Exception:
        pass
    try:
        return datetime.date.fromtimestamp(
            os.path.getmtime(os.path.join(ROOT, relpath))).isoformat()
    except OSError:
        return datetime.date.today().isoformat()
DOC_DIR = os.path.join(ROOT, "doc")
OUT_DIR = os.path.join(ROOT, "web", "docs")
SITE = "https://www.bovnar.io"
VERSION = "1.1"

# source, clean URL slug, PDF slug, title, meta description.
DOCS = [
    ("0_bovnar_tutorial.md", "tutorial", "bovnar-tutorial",
     "Bovnar Tutorial",
     "A guided, example-driven introduction to reading and writing Bovnar (BVNR) "
     "documents — types, units, arrays, references, and the streaming API."),
    ("1_bovnar_spec.md", "spec", "bovnar-specification",
     "Bovnar Specification (v1.1)",
     "The complete Bovnar (BVNR) format specification, version 1.1: grammar, type "
     "system, physical units, numeric bases, limits, and error semantics."),
    ("2_bovnar_unit_system.md", "units", "bovnar-unit-system",
     "Bovnar Unit & Currency System",
     "Every Bovnar physical unit, SI and IEC prefix, and currency — 163 units and "
     "216 currencies with dimensions and exact conversion factors."),
    ("3_bovnar_readwrite_api.md", "api", "bovnar-readwrite-api",
     "Bovnar C Read & Write API",
     "The Bovnar C reader, writer, and DOM API reference (bovnar.h, bovnar_dom.h), "
     "including read-time lossless unit and base conversion."),
    ("4_bovnar_python_bindings.md", "python", "bovnar-python-bindings",
     "Bovnar Python Bindings",
     "The pure-ctypes Bovnar Python package: loads/dumps, the streaming reader, the "
     "DOM, Quantity, and the NumPy and Pint bridges."),
    ("5_bovnar.ebnf", "grammar", "bovnar-grammar",
     "Bovnar EBNF Grammar",
     "The formal EBNF grammar of the Bovnar format, annotated against the reference "
     "lexer and validator."),
    ("6_bovnar_faq.md", "faq", "bovnar-faq",
     "Bovnar FAQ",
     "Frequently asked questions about Bovnar types, units, limits, encoding, and "
     "the C and Python APIs."),
    ("7_bovnar_conformance.md", "conformance", "bovnar-conformance",
     "Bovnar Conformance Testing",
     "The Bovnar 319-case conformance test suite and the IUT protocol for validating "
     "third-party implementations."),
    ("8_unit_cheatsheet.md", "cheatsheet", "bovnar-cheatsheet",
     "Bovnar Unit & Currency Cheat Sheet",
     "A one-page reference of every Bovnar unit symbol, SI/IEC prefix, and ISO 4217 "
     "and cryptocurrency code."),
    ("9_bovnar_streaming.md", "streaming", "bovnar-streaming",
     "Bovnar Streaming, Framing & Multiplexing",
     "Bovnar octet streams, frames, multiplexing/demultiplexing, and embedded "
     "documents for transport over sockets and files."),
]

# filename (and .ebnf) -> clean slug, for inter-doc link rewriting.
SRC_TO_SLUG = {src: slug for src, slug, *_ in DOCS}


def gh_slug(text):
    """GitHub-style heading slug: lowercase, drop punctuation (keep word chars,
    spaces, hyphens), spaces -> hyphens. Matches the anchors the raw Markdown
    already links to."""
    s = text.strip().lower()
    s = s.replace("`", "").replace("*", "")
    s = re.sub(r"<[^>]+>", "", s)
    s = re.sub(r"[^\w\s-]", "", s)
    return s.strip().replace(" ", "-")


class GHSlugSeen(dict):
    """python-markdown toc slugify gets (value, sep); GitHub disambiguates
    repeats with -1, -2, … We track counts across the whole document."""
    def __call__(self, value, sep):
        base = gh_slug(value)
        n = self.get(base, 0)
        self[base] = n + 1
        return base if n == 0 else f"{base}-{n}"


def render_markdown(text):
    md = markdown.Markdown(
        extensions=["extra", "sane_lists", "tables", "fenced_code",
                    "codehilite", "toc"],
        extension_configs={
            "codehilite": {"guess_lang": False, "noclasses": False},
            "toc": {"slugify": GHSlugSeen()},
        },
    )
    return md.convert(text)


def rewrite_links(body):
    """Point inter-doc Markdown links at their HTML pages."""
    def repl(m):
        href = m.group(1)
        frag = ""
        path = href
        if "#" in href:
            path, frag = href.split("#", 1)
            frag = "#" + frag
        # strip a leading ./ or /doc/ so both bare and absolute forms match
        key = path.rsplit("/", 1)[-1]
        slug = SRC_TO_SLUG.get(key)
        if slug:
            return f'href="{SITE}/docs/{slug}/{frag}"'
        return m.group(0)
    return re.sub(r'href="([^"]+)"', repl, body)


PYGMENTS_CSS = HtmlFormatter(style="default").get_style_defs(".codehilite")


CSS = """
:root{--bg:#fff;--fg:#1c2530;--muted:#5a6675;--border:#e2e6ea;--accent:#0a6cb0;
--code-bg:#f6f8fa;--nav-bg:#fbfcfd;}
@media (prefers-color-scheme:dark){:root{--bg:#14181d;--fg:#e6ebf0;--muted:#9aa6b2;
--border:#2a323b;--accent:#6cb0ea;--code-bg:#1c2229;--nav-bg:#1a1f25;}}
*{box-sizing:border-box}html{scroll-behavior:smooth}
body{margin:0;background:var(--bg);color:var(--fg);
font:16px/1.65 system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;}
header.site{position:sticky;top:0;background:var(--nav-bg);border-bottom:1px solid var(--border);
padding:.7rem 1.2rem;display:flex;gap:1rem;align-items:center;flex-wrap:wrap;z-index:10;}
header.site a{color:var(--fg);text-decoration:none}
header.site .brand{font-weight:600;display:flex;align-items:center;gap:.5rem}
header.site svg{width:26px;height:26px}
header.site nav{margin-left:auto;display:flex;gap:1rem;flex-wrap:wrap;font-size:.95rem}
header.site nav a:hover{color:var(--accent)}
main{max-width:52rem;margin:0 auto;padding:2rem 1.2rem 4rem}
main h1{font-size:2.1rem;letter-spacing:-.02em;margin:.2em 0 .1em}
.doc-meta{color:var(--muted);font-size:.92rem;margin-bottom:1.5rem;
border-bottom:1px solid var(--border);padding-bottom:1rem}
.doc-meta a{color:var(--accent)}
h2{margin-top:2.2em;border-bottom:1px solid var(--border);padding-bottom:.2em}
h3{margin-top:1.8em}
a{color:var(--accent)}
code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
background:var(--code-bg);padding:.12em .4em;border-radius:4px;font-size:.9em}
pre{background:var(--code-bg);border:1px solid var(--border);border-radius:8px;
padding:1rem;overflow-x:auto}
pre code{background:none;padding:0}
table{border-collapse:collapse;display:block;overflow-x:auto;max-width:100%}
th,td{border:1px solid var(--border);padding:.4em .7em;text-align:left}
th{background:var(--nav-bg)}
blockquote{margin:1em 0;padding:.4em 1em;border-left:3px solid var(--accent);
color:var(--muted);background:var(--code-bg);border-radius:0 6px 6px 0}
img{max-width:100%}
footer{max-width:52rem;margin:0 auto;padding:2rem 1.2rem;border-top:1px solid var(--border);
color:var(--muted);font-size:.9rem}
footer a{color:var(--accent)}
""" + PYGMENTS_CSS

MARK_SVG = (
    '<svg viewBox="0 0 64 64" aria-hidden="true"><defs><linearGradient id="g" '
    'x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#6cb0ea"/>'
    '<stop offset="1" stop-color="#0a85d6"/></linearGradient></defs>'
    '<path d="M28 13 L12.5 32 L28 51" fill="none" stroke="url(#g)" stroke-width="6.6" '
    'stroke-linecap="round" stroke-linejoin="round"/>'
    '<path d="M36 13 L51.5 32 L36 51" fill="none" stroke="url(#g)" stroke-width="6.6" '
    'stroke-linecap="round" stroke-linejoin="round"/>'
    '<path d="M32 20 L44 32 L32 44 L20 32 Z" fill="#33a78f"/>'
    '<path d="M32 20 L20 32 L32 32 Z" fill="#9cdcfe"/>'
    '<path d="M32 20 L44 32 L32 32 Z" fill="#4ec9b0"/>'
    '<path d="M20 32 L32 44 L32 32 Z" fill="#3fb89e"/>'
    '<path d="M44 32 L32 44 L32 32 Z" fill="#2c9281"/></svg>')

HEADER = (
    '<header class="site"><a class="brand" href="/">' + MARK_SVG +
    '<span>Bovnar</span></a><nav>'
    '<a href="/docs/">Docs</a><a href="/#playground">Playground</a>'
    '<a href="/#quickstart">Quick&nbsp;start</a>'
    '<a href="https://github.com/sothis/bovnar">GitHub</a></nav></header>')

FOOTER = (
    '<footer>Bovnar (BVNR) v' + VERSION +
    ' · <a href="/">Home</a> · <a href="/docs/">All docs</a> · '
    '<a href="https://github.com/sothis/bovnar">Source</a> · MIT-licensed'
    '</footer>')


def page(title, description, canonical, body, extra_head="", og_dates=None):
    esc = html.escape
    date_meta = ""
    if og_dates:
        pub, mod = og_dates
        date_meta = (f'<meta property="article:published_time" content="{pub}">\n'
                     f'<meta property="article:modified_time" content="{mod}">\n')
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; base-uri 'none'; form-action 'none'">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{esc(title)}</title>
<meta name="description" content="{esc(description)}">
<meta name="robots" content="index, follow">
<link rel="canonical" href="{canonical}">
<link rel="icon" type="image/svg+xml" href="/favicon.svg">
<link rel="icon" type="image/png" sizes="32x32" href="/favicon-32.png">
<meta property="og:type" content="article">
<meta property="og:site_name" content="Bovnar">
<meta property="og:title" content="{esc(title)}">
<meta property="og:description" content="{esc(description)}">
<meta property="og:url" content="{canonical}">
<meta property="og:image" content="{SITE}/bovnar-og.png">
<meta property="og:image:alt" content="Bovnar (BVNR) — unit-safe serialization">
{date_meta}
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="{esc(title)}">
<meta name="twitter:description" content="{esc(description)}">
<meta name="twitter:image" content="{SITE}/bovnar-og.png">
<meta name="twitter:image:alt" content="Bovnar (BVNR) — unit-safe serialization">
{extra_head}<style>{CSS}</style>
</head>
<body>
{HEADER}
<main>
{body}
</main>
{FOOTER}
</body>
</html>
"""


def faq_items(md_text):
    """(question, answer) pairs from the FAQ Markdown — bold questions ending in
    '?' followed by their answer, up to the next question or heading."""
    items = []
    pattern = re.compile(r"^\*\*(.+?\?)\*\*\s*$", re.M)
    matches = list(pattern.finditer(md_text))
    for i, m in enumerate(matches):
        q = m.group(1).strip()
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(md_text)
        ans = md_text[start:end]
        h = re.search(r"^#{1,6}\s", ans, re.M)
        if h:
            ans = ans[:h.start()]
        ans = re.sub(r"\s+", " ", re.sub(r"[`*#>|]", "", ans)).strip()
        if len(ans) < 20:
            continue
        items.append((q, ans[:600]))
    return items


def json_ld(obj):
    import json
    blob = json.dumps(obj, ensure_ascii=False).replace("</", "<\\/")
    return f'<script type="application/ld+json">{blob}</script>\n'


def breadcrumb(name, url):
    return {
        "@type": "BreadcrumbList",
        "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": "Home", "item": f"{SITE}/"},
            {"@type": "ListItem", "position": 2, "name": "Documentation",
             "item": f"{SITE}/docs/"},
            {"@type": "ListItem", "position": 3, "name": name, "item": url},
        ],
    }


# @id values mirror the entities defined in the landing page's JSON-LD graph, so
# search engines merge them into one site-wide entity graph across all pages.
AUTHOR = {"@type": "Person", "@id": f"{SITE}/#author", "name": "Janos Sonntag"}
PUBLISHER = {
    "@type": "Organization",
    "@id": f"{SITE}/#organization",
    "name": "Bovnar",
    "url": f"{SITE}/",
    "logo": {"@type": "ImageObject", "url": f"{SITE}/apple-touch-icon.png",
             "width": 180, "height": 180},
}
WEBSITE_REF = {"@type": "WebSite", "@id": f"{SITE}/#website",
               "name": "Bovnar", "url": f"{SITE}/"}


def doc_schema(title, desc, canonical, published, modified, faqs=None):
    """TechArticle + BreadcrumbList (+ FAQPage) structured data for a doc page.
    `title` is the clean doc title (no site suffix); published/modified are the
    git-derived ISO dates."""
    graph = [
        {
            "@type": "TechArticle",
            "headline": title,
            "description": desc,
            "url": canonical,
            "mainEntityOfPage": canonical,
            "inLanguage": "en",
            "image": [f"{SITE}/bovnar-og.png"],
            "datePublished": published,
            "dateModified": modified,
            "author": AUTHOR,
            "publisher": PUBLISHER,
            "isPartOf": WEBSITE_REF,
        },
        breadcrumb(title, canonical),
    ]
    if faqs:
        graph.append({
            "@type": "FAQPage",
            "mainEntity": [
                {"@type": "Question", "name": q,
                 "acceptedAnswer": {"@type": "Answer", "text": a}}
                for q, a in faqs
            ],
        })
    return json_ld({"@context": "https://schema.org", "@graph": graph})


def build_index_page():
    rows = []
    for src, slug, pdf, title, desc in DOCS:
        rows.append(
            f'<li><a href="/docs/{slug}/"><strong>{html.escape(title)}</strong></a>'
            f'<div class="d">{html.escape(desc)}</div>'
            f'<div class="fmt"><a href="/doc/{src}">Markdown</a> · '
            f'<a href="/doc/pdf/{pdf}.pdf">PDF</a></div></li>')
    body = (
        '<h1>Bovnar documentation</h1>'
        '<p class="doc-meta">Tutorial, specification, unit system, C and Python '
        'APIs, grammar, FAQ, and more — as HTML, Markdown, and PDF.</p>'
        '<ul class="doclist">' + "".join(rows) + '</ul>'
        '<style>.doclist{list-style:none;padding:0}.doclist li{padding:1rem 0;'
        'border-bottom:1px solid var(--border)}.doclist .d{color:var(--muted);'
        'margin:.2rem 0}.doclist .fmt{font-size:.85rem}</style>')
    schema = json_ld({
        "@context": "https://schema.org",
        "@graph": [
            {"@type": "CollectionPage", "url": f"{SITE}/docs/",
             "name": "Bovnar documentation", "inLanguage": "en",
             "isPartOf": WEBSITE_REF, "publisher": PUBLISHER},
            {"@type": "BreadcrumbList", "itemListElement": [
                {"@type": "ListItem", "position": 1, "name": "Home", "item": f"{SITE}/"},
                {"@type": "ListItem", "position": 2, "name": "Documentation",
                 "item": f"{SITE}/docs/"}]},
        ],
    })
    return page(
        "Documentation · Bovnar",
        "Bovnar (BVNR) documentation: tutorial, specification, unit system, C and "
        "Python APIs, grammar, FAQ, conformance, and streaming.",
        f"{SITE}/docs/", body, schema)


def build_doc_page(src, slug, pdf, title, desc):
    text = open(os.path.join(DOC_DIR, src), encoding="utf-8").read()
    extra_head = ""
    if src.endswith(".ebnf"):
        body_md = "```\n" + text + "\n```"
        h1 = title
        rendered = render_markdown(body_md)
    else:
        m = re.search(r"^#\s+(.*)$", text, re.M)
        h1 = m.group(1).strip() if m else title
        text_wo_h1 = re.sub(r"^#\s+.*$", "", text, count=1, flags=re.M)
        rendered = rewrite_links(render_markdown(text_wo_h1))
    canonical = f"{SITE}/docs/{slug}/"
    faqs = faq_items(text) if slug == "faq" else None
    published = git_date(f"doc/{src}", first=True)
    modified = git_date(f"doc/{src}")
    # Advertise this doc's Markdown counterpart for LLM/agent tools (RFC 7763).
    extra_head = (f'<link rel="alternate" type="text/markdown" href="/doc/{src}">\n'
                  + doc_schema(title, desc, canonical, published, modified, faqs))
    meta = (f'<p class="doc-meta">Bovnar (BVNR) v{VERSION} documentation · '
            f'Also available as <a href="/doc/{src}">Markdown</a> and '
            f'<a href="/doc/pdf/{pdf}.pdf">PDF</a>.</p>')
    body = f"<h1>{html.escape(h1)}</h1>\n{meta}\n{rendered}"
    # `title` already leads with "Bovnar", so no redundant "· Bovnar" suffix.
    return page(title, desc, canonical, body, extra_head, og_dates=(published, modified))


def main():
    check = "--check" in sys.argv[1:]
    targets = {}
    for src, slug, pdf, title, desc in DOCS:
        targets[os.path.join(OUT_DIR, slug, "index.html")] = \
            build_doc_page(src, slug, pdf, title, desc)
    targets[os.path.join(OUT_DIR, "index.html")] = build_index_page()

    if check:
        stale = [os.path.relpath(p, ROOT) for p, t in targets.items()
                 if (not os.path.exists(p)) or open(p, encoding="utf-8").read() != t]
        if stale:
            print("gen_html_docs.py --check: stale/missing: " + ", ".join(stale),
                  file=sys.stderr)
            return 1
        print(f"gen_html_docs.py --check: {len(targets)} HTML doc pages are current")
        return 0

    print("==> Generating HTML doc pages (gen_html_docs.py)")
    for path, text in targets.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
    print(f"  wrote {len(targets)} pages under web/docs/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
