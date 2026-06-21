#!/usr/bin/env python3
"""
Generate downloadable PDF versions of the Bovnar documentation.

Renders each Markdown doc in doc/ (plus the EBNF grammar) to a styled,
print-ready PDF under build/doc/pdf/, branded to match the website, and
bundles them all into build/doc/pdf/bovnar-docs-pdf.zip.

All output goes under build/ (git-ignored); the canonical sources are the
Markdown files in doc/. The PDFs are not kept in the repository — publish_web.sh
copies them into the site's doc/pdf/ path at upload time (use --pdf to rebuild).

Dependencies (not part of the library/Python-package requirements; install in a
throwaway venv to run this):

    pip install weasyprint markdown pygments

WeasyPrint needs the usual pango/cairo/gdk-pixbuf system libraries.

Usage:  python gen_doc_pdfs.py
"""
import os
import re
import datetime
import zipfile
import markdown
from weasyprint import HTML
from pygments.formatters import HtmlFormatter

ROOT = os.path.dirname(os.path.abspath(__file__))
DOC_DIR = os.path.join(ROOT, "doc")
OUT_DIR = os.path.join(ROOT, "build", "doc", "pdf")
ZIP_NAME = "bovnar-docs-pdf.zip"

VERSION = "1.1"

# source filename -> (output slug, short label used in the footer/header)
DOCS = [
    ("0_bovnar_tutorial.md",        "bovnar-tutorial",         "Tutorial"),
    ("1_bovnar_spec.md",            "bovnar-specification",    "Specification"),
    ("2_bovnar_unit_system.md",     "bovnar-unit-system",      "Unit & Currency Reference"),
    ("3_bovnar_readwrite_api.md",   "bovnar-readwrite-api",    "Read & Write API"),
    ("4_bovnar_python_bindings.md", "bovnar-python-bindings",  "Python Bindings"),
    ("5_bovnar.ebnf",               "bovnar-grammar",          "EBNF Grammar"),
    ("6_bovnar_faq.md",             "bovnar-faq",              "FAQ"),
    ("7_bovnar_conformance.md",     "bovnar-conformance",      "Conformance Test Tool"),
    ("8_unit_cheatsheet.md",        "bovnar-cheatsheet",       "Units & Currencies Cheat Sheet"),
    ("10_bovnar_streaming.md",      "bovnar-streaming",        "Streaming, Framing & Multiplexing"),
]

PYGMENTS_CSS = HtmlFormatter(style="default").get_style_defs(".codehilite")

# ── Brand palette (matches the website: blue logo gradient + teal accent) ──
#   --brand      primary blue  (logo gradient deep stop)
#   --brand-dk   darker blue   (rules, links on white — AA contrast)
#   --teal       secondary accent (code rail, callout rail)
#   --ink        heading/near-black text
#   --body       body text
#   --muted      captions, page furniture
#   --hair       hairline borders
#   --tint       light blue-grey fills (table head, zebra, callouts)
CSS = r"""
@page {
    size: A4;
    margin: 24mm 18mm 20mm 18mm;
    @top-right {
        content: "BOVNAR";
        font-family: 'DejaVu Sans Mono', monospace;
        font-size: 7.5pt;
        letter-spacing: 2.5px;
        color: #0a6cb0;
    }
    @bottom-left {
        content: "Bovnar v__VERSION__  ·  __LABEL__";
        font-size: 7.5pt;
        color: #8a929c;
    }
    @bottom-right {
        content: counter(page) " / " counter(pages);
        font-size: 7.5pt;
        color: #8a929c;
    }
}
@page :first { @top-right { content: ""; } }

html { font-size: 10.5pt; }
body {
    font-family: 'DejaVu Sans', 'Helvetica Neue', Arial, sans-serif;
    color: #1c2128;
    line-height: 1.55;
    hyphens: auto;
    text-rendering: optimizeLegibility;
}

/* ── Cover block ─────────────────────────────────────────────── */
.cover {
    border-bottom: 2.5px solid #0a6cb0;
    margin-bottom: 1.6em;
    padding-bottom: 0.9em;
}
.cover .mark {
    font-family: 'DejaVu Sans Mono', monospace;
    font-size: 9pt;
    font-weight: bold;
    letter-spacing: 3.5px;
    color: #0a6cb0;
    margin: 0 0 0.45em 0;
    text-transform: uppercase;
}
.cover h1 { margin: 0; }
.cover .subtitle {
    color: #57606a;
    font-size: 9.5pt;
    margin: 0.55em 0 0 0;
    letter-spacing: 0.2px;
}

/* ── Headings ────────────────────────────────────────────────── */
h1 { font-size: 23pt; color: #0f1722; margin: 0; line-height: 1.12;
     font-weight: bold; letter-spacing: -0.3pt; }
h2 { font-size: 15pt; color: #0f1722; margin: 1.55em 0 0.5em;
     border-bottom: 1px solid #dfe3e8; padding-bottom: 0.22em;
     letter-spacing: -0.2pt; }
h3 { font-size: 12.5pt; color: #16324a; margin: 1.15em 0 0.32em; }
h4 { font-size: 10.8pt; color: #16324a; margin: 0.95em 0 0.22em;
     letter-spacing: 0.2px; }
h2, h3, h4 { break-after: avoid; }
h1 + *, h2 + *, h3 + *, h4 + * { break-before: avoid; }

p, li { orphans: 2; widows: 2; }
ul, ol { padding-left: 1.5em; }
li { margin: 0.18em 0; }
li > ul, li > ol { margin-top: 0.18em; }
strong, b { color: #11202e; }
a { color: #0a5e9c; text-decoration: none; }

/* ── Code ────────────────────────────────────────────────────── */
code {
    font-family: 'DejaVu Sans Mono', 'Courier New', monospace;
    font-size: 8.8pt;
    background: #eef2f6;
    border: 1px solid #dce3ea;
    border-radius: 3px;
    padding: 0.05em 0.32em;
    color: #143247;
}
pre {
    background: #f6f8fa;
    border: 1px solid #dde4ea;
    border-left: 3px solid #1f9e86;
    border-radius: 4px;
    padding: 0.75em 0.95em;
    font-size: 8.5pt;
    line-height: 1.45;
    white-space: pre-wrap;
    word-break: break-word;
    break-inside: avoid;
}
pre code { background: none; border: none; padding: 0; font-size: inherit; color: inherit; }

/* ── Tables (zebra-striped for the large unit/currency listings) ─ */
table {
    border-collapse: collapse;
    width: 100%;
    margin: 0.8em 0;
    font-size: 8.8pt;
    break-inside: auto;
}
th, td { border: 1px solid #e3e8ee; padding: 4.5px 8px; text-align: left;
         vertical-align: top; word-break: break-word; }
th { background: #e9f1f8; color: #0f1722; font-weight: bold;
     border-bottom: 1.5px solid #b9d4e8; }
tbody tr:nth-child(even) td { background: #f5f8fb; }
tr { break-inside: avoid; }
thead { display: table-header-group; }

/* ── Callouts / metadata blockquotes ─────────────────────────── */
blockquote {
    margin: 0.8em 0; padding: 0.45em 1em;
    border-left: 3px solid #0a6cb0; color: #44505c; background: #f1f6fb;
    border-radius: 0 4px 4px 0;
}
blockquote p { margin: 0.2em 0; }
hr { border: none; border-top: 1px solid #dfe3e8; margin: 1.5em 0; }
img { max-width: 100%; }
""" + "\n" + PYGMENTS_CSS


def render_markdown(text):
    md = markdown.Markdown(extensions=[
        "extra", "sane_lists", "tables", "fenced_code", "codehilite", "toc",
    ], extension_configs={
        "codehilite": {"guess_lang": False, "noclasses": False},
    })
    return md.convert(text)


def build_html(title, subtitle, body_html, label):
    css = CSS.replace("__VERSION__", VERSION).replace("__LABEL__", label)
    cover = (f'<div class="cover"><p class="mark">BOVNAR · v{VERSION}</p>'
             f'<h1>{title}</h1>'
             f'<p class="subtitle">{subtitle}</p></div>')
    return (f"<!DOCTYPE html><html><head><meta charset='utf-8'>"
            f"<title>{title}</title><style>{css}</style></head>"
            f"<body>{cover}{body_html}</body></html>")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    today = datetime.date.today().isoformat()
    written = []
    for src, slug, label in DOCS:
        path = os.path.join(DOC_DIR, src)
        text = open(path, encoding="utf-8").read()
        if src.endswith(".ebnf"):
            title = "Bovnar — EBNF Grammar"
            body = render_markdown("```\n" + text + "\n```")
        else:
            m = re.search(r"^#\s+(.*)$", text, re.M)
            title = m.group(1).strip() if m else slug
            # drop the leading H1 (used as the cover title) and any leading
            # version/blockquote lines are kept as-is.
            text = re.sub(r"^#\s+.*$", "", text, count=1, flags=re.M)
            body = render_markdown(text)
        subtitle = f"Bovnar (BVNR) v{VERSION} documentation · {label} · {today}"
        html = build_html(title, subtitle, body, label)
        out = os.path.join(OUT_DIR, slug + ".pdf")
        HTML(string=html, base_url=DOC_DIR).write_pdf(out)
        written.append(out)
        kb = os.path.getsize(out) // 1024
        print(f"  {slug + '.pdf':32s} {kb:5d} KB   <- {src}")

    # Bundle every generated PDF into a single flat zip for the "download all"
    # link on the website. Sorted by name so the archive is reproducible.
    zip_path = os.path.join(OUT_DIR, ZIP_NAME)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for pdf in sorted(written, key=os.path.basename):
            zf.write(pdf, arcname=os.path.basename(pdf))
    kb = os.path.getsize(zip_path) // 1024
    print(f"  {ZIP_NAME:32s} {kb:5d} KB   <- {len(written)} PDFs")


if __name__ == "__main__":
    main()
