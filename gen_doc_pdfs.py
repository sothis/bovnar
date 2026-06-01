#!/usr/bin/env python3
"""
Generate downloadable PDF versions of the Bovnar documentation.

Renders each Markdown doc in web/doc/ (plus the EBNF grammar) to a styled,
print-ready PDF in web/doc/pdf/, branded to match the website.

Dependencies (not part of the library/Python-package requirements; install in a
throwaway venv to run this):

    pip install weasyprint markdown pygments

WeasyPrint needs the usual pango/cairo/gdk-pixbuf system libraries.

Usage:  python gen_doc_pdfs.py
"""
import os
import re
import datetime
import markdown
from weasyprint import HTML
from pygments.formatters import HtmlFormatter

ROOT = os.path.dirname(os.path.abspath(__file__))
DOC_DIR = os.path.join(ROOT, "web", "doc")
OUT_DIR = os.path.join(DOC_DIR, "pdf")

VERSION = "1.0"

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
]

PYGMENTS_CSS = HtmlFormatter(style="friendly").get_style_defs(".codehilite")

CSS = r"""
@page {
    size: A4;
    margin: 24mm 18mm 20mm 18mm;
    @top-right {
        content: "BOVNAR";
        font-family: 'DejaVu Sans Mono', monospace;
        font-size: 8pt;
        letter-spacing: 2px;
        color: #b14a86;
    }
    @bottom-center {
        content: "Bovnar v__VERSION__  ·  __LABEL__";
        font-size: 8pt;
        color: #8a8f98;
    }
    @bottom-right {
        content: counter(page) " / " counter(pages);
        font-size: 8pt;
        color: #8a8f98;
    }
}
@page :first { @top-right { content: ""; } }

html { font-size: 10.5pt; }
body {
    font-family: 'DejaVu Sans', 'Helvetica Neue', Arial, sans-serif;
    color: #1c2128;
    line-height: 1.5;
    hyphens: auto;
}
.cover {
    border-bottom: 3px solid #d6428a;
    margin-bottom: 1.4em;
    padding-bottom: 0.7em;
}
.cover .mark {
    font-family: 'DejaVu Sans Mono', monospace;
    font-size: 10pt;
    letter-spacing: 4px;
    color: #d6428a;
    margin: 0 0 0.2em 0;
}
.cover .subtitle { color: #57606a; font-size: 10pt; margin: 0.3em 0 0 0; }

h1 { font-size: 22pt; color: #11151c; margin: 0; line-height: 1.15; }
h2 { font-size: 15pt; color: #11151c; margin: 1.4em 0 0.4em;
     border-bottom: 1px solid #e2e6ea; padding-bottom: 0.2em; }
h3 { font-size: 12.5pt; color: #24292f; margin: 1.1em 0 0.3em; }
h4 { font-size: 11pt; color: #24292f; margin: 0.9em 0 0.2em; }
h2, h3, h4 { break-after: avoid; }

p, li { orphans: 2; widows: 2; }
a { color: #b14a86; text-decoration: none; }

code {
    font-family: 'DejaVu Sans Mono', 'Courier New', monospace;
    font-size: 8.8pt;
    background: #f3f1f5;
    border: 1px solid #e7e2ec;
    border-radius: 3px;
    padding: 0.05em 0.3em;
}
pre {
    background: #f7f6f9;
    border: 1px solid #e3dfe9;
    border-left: 3px solid #d6428a;
    border-radius: 4px;
    padding: 0.7em 0.9em;
    font-size: 8.5pt;
    line-height: 1.4;
    white-space: pre-wrap;
    word-break: break-word;
    break-inside: avoid;
}
pre code { background: none; border: none; padding: 0; font-size: inherit; }

table {
    border-collapse: collapse;
    width: 100%;
    margin: 0.7em 0;
    font-size: 8.8pt;
    break-inside: auto;
}
th, td { border: 1px solid #dfe3e8; padding: 4px 7px; text-align: left;
         vertical-align: top; word-break: break-word; }
th { background: #f3eef6; color: #2a2230; }
tr { break-inside: avoid; }

blockquote {
    margin: 0.7em 0; padding: 0.3em 0.9em;
    border-left: 3px solid #d6c7da; color: #4a515a; background: #faf8fc;
}
hr { border: none; border-top: 1px solid #e2e6ea; margin: 1.4em 0; }
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
        kb = os.path.getsize(out) // 1024
        print(f"  {slug + '.pdf':32s} {kb:5d} KB   <- {src}")


if __name__ == "__main__":
    main()
