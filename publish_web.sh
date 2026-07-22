#!/usr/bin/env bash
#
# publish_web.sh — deploy the Bovnar website (everything under ./web) to the
# live server's web root with rsync.
#
# The web/doc directory is a symlink to ../doc (the canonical Markdown sources).
# This script stages the site into a temp directory, *resolving that symlink* so
# the docs land as real files under <webroot>/doc — never as a dangling link.
#
# The documentation PDFs and the bovnar-docs-pdf.zip bundle are NOT kept in the
# repository; they are generated under build/doc/pdf/ (git-ignored). Pass --pdf
# to (re)build them with gen_doc_pdfs.py before uploading. Without --pdf the
# script ships whatever already exists in build/doc/pdf/ (and warns if empty).
#
# Usage:
#   ./publish_web.sh [options] [user@host:/var/www/html]
#
# The destination may be given as the last argument, or via the
# BOVNAR_PUBLISH_DEST environment variable. The argument wins if both are set.
#
# Options:
#   --pdf            Regenerate the doc PDFs + zip bundle before publishing.
#   --delete         Pass --delete to rsync (prune files removed locally).
#   -n, --dry-run    Show what rsync would transfer; do not build or upload.
#   -h, --help       Show this help and exit.
#
# Examples:
#   ./publish_web.sh me@bovnar.org:/var/www/html
#   ./publish_web.sh --pdf me@bovnar.org:/var/www/html
#   ./publish_web.sh --pdf --delete me@bovnar.org:/var/www/html
#   BOVNAR_PUBLISH_DEST=me@bovnar.org:/var/www/html ./publish_web.sh --pdf
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$ROOT/web"
GEN_PDF="$ROOT/gen_doc_pdfs.py"
PDF_BUILD_DIR="$ROOT/build/doc/pdf"   # where gen_doc_pdfs.py writes its output

# Paths (relative to the web root) that must NOT be published. These are working
# documents and dev helpers that live under web/ or under doc/ (the symlink
# target) but are not part of the public site.
EXCLUDES=(
    "doc/iana_media_type.md"
    "doc/datetime_fractional_seconds.md"
    "doc/bovnar_pipeline.svg"
    "httpd.sh"
    # Translation SOURCE tables. They are a build input -- gen_i18n.py has
    # already baked them into web/<lang>/index.html by the time we stage -- and
    # they carry the _orphaned graveyard of superseded strings. There is no
    # reason to serve them from the live root.
    "i18n"
)

BUILD_PDF=0
DRY_RUN=0
RSYNC_DELETE=0
DEST="${BOVNAR_PUBLISH_DEST:-}"

# 2..(last comment line) — printing past the header dumped shell source.
usage() { sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --pdf|--build-pdf) BUILD_PDF=1 ;;
        --delete)          RSYNC_DELETE=1 ;;
        -n|--dry-run)      DRY_RUN=1 ;;
        -h|--help)         usage; exit 0 ;;
        -*) echo "publish_web.sh: unknown option: $1" >&2; exit 2 ;;
        *)  DEST="$1" ;;
    esac
    shift
done

if [ -z "$DEST" ]; then
    echo "publish_web.sh: no destination given." >&2
    echo "Pass it as an argument or set BOVNAR_PUBLISH_DEST, e.g.:" >&2
    echo "  ./publish_web.sh me@host:/var/www/html" >&2
    exit 2
fi

# ── 1. Optionally (re)build the PDFs + zip into build/doc/pdf/ ───────────────
if [ "$BUILD_PDF" -eq 1 ]; then
    echo "==> Building documentation PDFs + zip (gen_doc_pdfs.py)"
    if [ "$DRY_RUN" -eq 1 ]; then
        echo "    [dry-run] would run: python3 $GEN_PDF"
    else
        python3 "$GEN_PDF"
    fi
fi

# Warn if we are about to ship a site with no PDFs at all.
if ! ls "$PDF_BUILD_DIR"/*.pdf >/dev/null 2>&1; then
    echo "WARNING: no PDFs in build/doc/pdf/ — the download links will 404." >&2
    echo "         Re-run with --pdf to generate them." >&2
fi

# ── 1b. Regenerate the translated editions ──────────────────────────────────
# web/<lang>/index.html is generated, git-ignored, and rebuilt on every publish,
# so a translated page can never lag behind the English source it is spliced
# from. gen_i18n.py exits non-zero if any string is untranslated or if a script
# path would break one directory down, which fails the publish before upload.
for _lang_table in "$WEB_DIR"/i18n/*.json; do
    [ -e "$_lang_table" ] || continue
    _lang="$(basename "$_lang_table" .json)"
    echo "==> Generating web/$_lang/ from index.html + i18n/$_lang.json"
    if [ "$DRY_RUN" -eq 1 ]; then
        echo "    [dry-run] would run: python3 $ROOT/gen_i18n.py $_lang"
        python3 "$ROOT/gen_i18n.py" --check "$_lang"
    else
        python3 "$ROOT/gen_i18n.py" "$_lang"
    fi
done

# ── 2. Stage the site, resolving the web/doc symlink to real files ──────────
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/bovnar-publish.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

# -L dereferences symlinks (so web/doc -> ../doc becomes a real doc/ tree).
cp -rL "$WEB_DIR"/. "$STAGE"/

# Drop in the generated PDFs (they live under build/, not in the repo tree).
if ls "$PDF_BUILD_DIR"/* >/dev/null 2>&1; then
    mkdir -p "$STAGE/doc/pdf"
    cp "$PDF_BUILD_DIR"/* "$STAGE/doc/pdf/"
fi

# Drop excluded paths from the staging tree.
for rel in "${EXCLUDES[@]}"; do
    rm -rf "$STAGE/$rel"
done

# ── 3. Upload with rsync ────────────────────────────────────────────────────
# --chmod normalises permissions on the destination: directories 755, files 644.
# Without it, `-a` (which implies -p) would copy the staging dir's own mode onto
# the web root — and `mktemp -d` makes that 0700, leaving /var/www/html
# un-traversable by the web server (403). Forcing world-readable/-traversable
# perms here is both the fix and idempotent on every subsequent publish.
RSYNC_OPTS=(-az --human-readable --chmod=D755,F644)
# --delete prunes everything at the destination that is not in the staging tree,
# and the staging tree is only what lives under web/. Anything the SERVER owns
# must be protected explicitly -- above all .well-known/acme-challenge, which is
# where certbot writes ACME challenges: deleting it breaks TLS renewal. Use
# --delete-after so a transfer that fails partway does not leave the live site
# with files already removed.
[ "$RSYNC_DELETE" -eq 1 ] && RSYNC_OPTS+=(--delete-after --exclude='/.well-known/')
[ "$DRY_RUN" -eq 1 ] && RSYNC_OPTS+=(--dry-run --itemize-changes)

echo "==> Publishing to: $DEST"
[ "$DRY_RUN" -eq 1 ] && echo "    [dry-run] rsync ${RSYNC_OPTS[*]} <staged web/> $DEST/"

# Trailing slash on the source = copy the *contents* of the staging dir into
# the destination directory (not the staging dir itself).
rsync "${RSYNC_OPTS[@]}" "$STAGE"/ "$DEST"/

[ "$DRY_RUN" -eq 1 ] || echo "==> Done."
