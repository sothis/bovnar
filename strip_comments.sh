#!/usr/bin/env bash
# strip_comments.sh — Strip C99 comments and normalize whitespace/line endings.
#
# Processing pipeline (all in the embedded Python3 module):
#
#   1. Normalize line endings
#        \r\n  → \n      (Windows CRLF)
#        \r    → \n      (old Mac CR-only)
#
#   2. Strip comments  (ISO C99 §5.1.1.2 translation phase 3)
#        /* … */  → one space character (even across multiple lines)
#        // … \n  → consumed; the terminating \n is kept
#        Newlines inside block comments are preserved at this stage so the
#        post-processing pass can reason about them correctly.
#        String and character literals are never modified.
#
#   3. Strip trailing whitespace from every line
#        All horizontal whitespace (SP, TAB, …) immediately before \n or EOF
#        is removed.  \n itself is not whitespace for this purpose.
#
#   4. Remove empty lines
#        Any line that is empty after steps 2-3 is dropped.  This eliminates
#        "remnant" lines whose entire content was a comment (e.g. a standalone
#        // line or a /* */ that spanned one or more lines on its own).
#
#   5. Ensure exactly one trailing newline
#        The output always ends with exactly one \n.
#
# Why not GCC/Clang?  No single flag combination strips only comments:
#   -E -fpreprocessed -P   → strips comments but silently drops #define lines
#   -E -fdirectives-only   → does NOT strip comments at all
#   -E -P                  → strips comments but also fully expands macros
#
# Requires: bash ≥ 4, python3 (≥ 3.6) in PATH.  No C compiler needed.
#
# Usage
# -----
#   strip_comments.sh [OPTIONS] FILE [FILE …]
#   strip_comments.sh [OPTIONS] -              (C source from stdin)
#   find DIR … | strip_comments.sh [OPTIONS]   (filenames from stdin, LF-delimited)
#   find DIR … -print0 | strip_comments.sh -0 [OPTIONS]  (NUL-delimited)
#
# Typical find invocations:
#   find src \( -iname "*.c" -o -iname "*.h" \) | strip_comments.sh -i.bak
#   find src \( -iname "*.c" -o -iname "*.h" \) -print0 | strip_comments.sh -0 -i
#   find src \( -iname "*.c" -o -iname "*.h" \) -exec strip_comments.sh -i.bak {} +
#
# Options:
#   -o OUTPUT   Write output to OUTPUT (single input only; exclusive with -i)
#   -i[SUFFIX]  Edit in-place.  SUFFIX (no space) names a backup extension.
#               Without SUFFIX the original is overwritten directly (no backup).
#   -0          Read NUL-delimited filenames from stdin instead of LF-delimited.
#               Pairs with find -print0 or xargs -0.  Implies reading filenames
#               from stdin; must not be combined with explicit FILE arguments.
#   -v          Verbose: echo each filename to stderr.
#   -h          Show this help.
#
# Stdin dispatch:
#   • Explicit FILE arguments given → stdin is not read for filenames.
#   • Explicit '-' as a FILE argument → read C source from stdin.
#   • No FILE arguments, stdin is a pipe → read filenames from stdin.
#   • No FILE arguments, stdin is a terminal → silent no-op (exit 0);
#     safe when xargs calls the script with an empty find result.
#
# Exit status
#   0   all files processed successfully (including the empty-input no-op)
#   1   usage error
#   2   python3 (≥ 3.6) not found in PATH
#   3   one or more files failed (processing continues for remaining files)

set -euo pipefail

prog="$(basename "$0")"
die()  { printf '%s: error: %s\n' "$prog" "$*" >&2; exit 1; }
warn() { printf '%s: warning: %s\n' "$prog" "$*" >&2; }

usage() {
    sed -n '/^# Usage/,/^# Exit/{ /^# Exit/d; s/^# \{0,1\}//; p }' "$0" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Embedded Python3 module
#
# Materialised into a tempfile at startup so that stdin remains available for
# two distinct purposes depending on how the script is called:
#   • reading C source when an explicit '-' is passed as a FILE argument, or
#   • reading LF/NUL-delimited filenames when no FILE arguments are given.
# Using `python3 - <<< code` would consume stdin before either purpose runs.
# ---------------------------------------------------------------------------

read -r -d '' PY_LEXER << 'PYEOF' || true
import sys, io, re

# ---------------------------------------------------------------------------
# Step 1 — normalize line endings to \n
# ---------------------------------------------------------------------------

def normalize_endings(src: str) -> str:
    return src.replace('\r\n', '\n').replace('\r', '\n')

# ---------------------------------------------------------------------------
# Step 2 — ISO C99 §5.1.1.2 phase-3 comment stripper
#
# States:
#   NORMAL         ordinary source text
#   SLASH          seen '/', pending decision (comment start vs lone slash)
#   LINE_CMT       inside // … comment; consume until \n, then keep the \n
#   BLOCK_CMT      inside /* … comment
#   BLOCK_CMT_STAR inside /* … */, seen '*' (possible closing '*/')
#   STRING         inside "…" string literal
#   STRING_ESC     inside "…", after '\' (next char is literal)
#   CHAR_LIT       inside '…' character constant
#   CHAR_ESC       inside '…', after '\'
#
# Per ISO C99 §5.1.1.2:
#   • A block comment is replaced by one space character.
#   • Newlines within a block comment are preserved here so step 4 can count
#     them as empty lines and remove them.
#   • A line comment's terminating \n is kept (it is not part of the comment).
# ---------------------------------------------------------------------------

def strip_comments(src: str) -> str:
    out = io.StringIO()
    i   = 0
    n   = len(src)

    NORMAL         = 0
    SLASH          = 1
    LINE_CMT       = 2
    BLOCK_CMT      = 3
    BLOCK_CMT_STAR = 4
    STRING         = 5
    STRING_ESC     = 6
    CHAR_LIT       = 7
    CHAR_ESC       = 8

    state = NORMAL

    while i < n:
        c = src[i]

        if state == NORMAL:
            if   c == '/':   state = SLASH
            elif c == '"':   out.write(c); state = STRING
            elif c == "'":   out.write(c); state = CHAR_LIT
            else:            out.write(c)

        elif state == SLASH:
            if   c == '/':   state = LINE_CMT
            elif c == '*':   state = BLOCK_CMT
            else:
                out.write('/')
                out.write(c)
                state = NORMAL

        elif state == LINE_CMT:
            if c == '\n':
                out.write('\n')
                state = NORMAL

        elif state == BLOCK_CMT:
            if   c == '*':   state = BLOCK_CMT_STAR
            elif c == '\n':  out.write('\n')

        elif state == BLOCK_CMT_STAR:
            if   c == '/':
                out.write(' ')
                state = NORMAL
            elif c == '*':   pass
            else:
                if c == '\n': out.write('\n')
                state = BLOCK_CMT

        elif state == STRING:
            out.write(c)
            if   c == '\\':  state = STRING_ESC
            elif c == '"':   state = NORMAL

        elif state == STRING_ESC:
            out.write(c)
            state = STRING

        elif state == CHAR_LIT:
            out.write(c)
            if   c == '\\':  state = CHAR_ESC
            elif c == "'":   state = NORMAL

        elif state == CHAR_ESC:
            out.write(c)
            state = CHAR_LIT

        i += 1

    if state in (BLOCK_CMT, BLOCK_CMT_STAR):
        out.write(' ')
    if state == SLASH:
        out.write('/')

    return out.getvalue()

# ---------------------------------------------------------------------------
# Steps 3-5 — trailing-whitespace strip, empty-line removal, single final \n
#
# "Horizontal whitespace" here means any character that is whitespace but
# is not \n.  We use a regex character class rather than str.rstrip() so
# that we never accidentally consume a \n that belongs to line structure.
#
# Empty-line removal discards lines that have no non-whitespace content after
# comment stripping.  These are the "remnant" lines whose sole content was a
# comment.  The result is split on \n, each line's trailing horizontal space
# is removed, blank lines are discarded, and the remainder is rejoined with
# \n followed by exactly one final \n.
# ---------------------------------------------------------------------------

_TRAILING_HWS = re.compile(r'[^\S\n]+$', re.MULTILINE)

def cleanup(src: str) -> str:
    stripped = _TRAILING_HWS.sub('', src)
    lines    = [l for l in stripped.split('\n') if l]
    return '\n'.join(lines) + '\n' if lines else ''

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    infile  = sys.argv[1] if len(sys.argv) > 1 else '-'
    outfile = sys.argv[2] if len(sys.argv) > 2 else '-'

    if infile == '-':
        raw = sys.stdin.buffer.read().decode(errors='surrogateescape')
    else:
        with open(infile, 'rb') as fh:
            raw = fh.read().decode(errors='surrogateescape')

    result = cleanup(strip_comments(normalize_endings(raw)))

    encoded = result.encode(errors='surrogateescape')
    if outfile == '-':
        sys.stdout.buffer.write(encoded)
        sys.stdout.buffer.flush()
    else:
        with open(outfile, 'wb') as fh:
            fh.write(encoded)
PYEOF

# ---------------------------------------------------------------------------
# Option parsing
# ---------------------------------------------------------------------------

OPT_OUTPUT=""
OPT_INPLACE=0
OPT_SUFFIX=""
OPT_NULL=0
OPT_VERBOSE=0

# Peel -i[SUFFIX] before getopts because getopts cannot handle optional args.
ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -i)    OPT_INPLACE=1; OPT_SUFFIX="";         shift ;;
        -i?*)  OPT_INPLACE=1; OPT_SUFFIX="${1#-i}";  shift ;;
        --)    shift; ARGS+=("$@"); break ;;
        *)     ARGS+=("$1"); shift ;;
    esac
done
set -- "${ARGS[@]+"${ARGS[@]}"}"

while getopts ":o:0vh" opt; do
    case $opt in
        o)  OPT_OUTPUT="$OPTARG" ;;
        0)  OPT_NULL=1 ;;
        v)  OPT_VERBOSE=1 ;;
        h)  usage ;;
        :)  die "option -$OPTARG requires an argument" ;;
        \?) die "unknown option: -$OPTARG" ;;
    esac
done
shift $((OPTIND - 1))

[[ -n "$OPT_OUTPUT" && $OPT_INPLACE -eq 1 ]] && die "-o and -i are mutually exclusive"
[[ $OPT_NULL -eq 1 && $# -gt 0 ]]            && die "-0 reads filenames from stdin; do not also pass FILE arguments"

# ---------------------------------------------------------------------------
# Stdin dispatch
#
# Three cases once explicit FILE arguments are absent:
#   a) -0 flag set            → read NUL-delimited filenames from stdin
#   b) stdin is a pipe/file   → read LF-delimited filenames from stdin
#   c) stdin is a terminal    → no input; silent no-op so xargs is safe when
#                               find produces an empty result set
#
# After mapfile, stdin is fully consumed and at EOF; the Python lexer never
# touches stdin in these paths (it only reads stdin when passed '-' as a path,
# which cannot happen here because find never emits a bare '-' as a filename).
# ---------------------------------------------------------------------------

if [[ $# -eq 0 ]]; then
    if [[ $OPT_NULL -eq 1 ]] || [[ ! -t 0 ]]; then
        mapfile -d $'\0' -t _files < <(
            if [[ $OPT_NULL -eq 1 ]]; then
                cat        # already NUL-delimited; pass through raw
            else
                # Convert LF-delimited to NUL-delimited so mapfile -d $'\0'
                # handles any embedded whitespace in paths correctly.
                tr '\n' '\0'
            fi
        )
        set -- "${_files[@]+"${_files[@]}"}"
        unset _files
    fi
    # Terminal with no args → fall through with $# still 0; main loop is a no-op.
fi

[[ -n "$OPT_OUTPUT" && $# -gt 1 ]] && die "-o requires exactly one input file"

# ---------------------------------------------------------------------------
# Dependency check
# ---------------------------------------------------------------------------

PYTHON3=""
for py in python3 python; do
    if command -v "$py" &>/dev/null \
       && "$py" -c 'import sys; assert sys.version_info >= (3,6)' 2>/dev/null
    then
        PYTHON3="$py"
        break
    fi
done
[[ -n "$PYTHON3" ]] \
    || { printf '%s: error: python3 (>=3.6) not found in PATH\n' "$prog" >&2; exit 2; }

PY_TMPFILE="$(mktemp /tmp/.strip_lexer_XXXXXX.py)"
trap 'rm -f "$PY_TMPFILE"' EXIT
printf '%s\n' "$PY_LEXER" > "$PY_TMPFILE"

# ---------------------------------------------------------------------------
# Per-file processing
# ---------------------------------------------------------------------------

strip_file() {
    local input="$1"
    local output="$2"

    if [[ "$input" != "-" ]]; then
        [[ -f "$input" ]] || { warn "'$input': not found or not a regular file"; return 3; }
        [[ -r "$input" ]] || { warn "'$input': permission denied"; return 3; }
        [[ $OPT_VERBOSE -eq 1 ]] && printf 'stripping: %s\n' "$input" >&2
    fi

    local py_out="${output:--}"
    if ! "$PYTHON3" "$PY_TMPFILE" "$input" "$py_out"; then
        warn "'$input': lexer returned non-zero"
        return 3
    fi
}

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

exit_status=0

for input in "$@"; do
    if [[ $OPT_INPLACE -eq 1 ]]; then
        tmpfile="$(mktemp -- "$(dirname "$input")/.strip_XXXXXX")"
        trap 'rm -f "$tmpfile"' TERM INT

        if strip_file "$input" "$tmpfile"; then
            if [[ -n "$OPT_SUFFIX" ]]; then
                cp --preserve=all -- "$input" "${input}${OPT_SUFFIX}"
            fi
            chmod --reference="$input" -- "$tmpfile" 2>/dev/null || true
            mv -- "$tmpfile" "$input"
        else
            rm -f "$tmpfile"
            exit_status=3
        fi

        trap - TERM INT

    elif [[ -n "$OPT_OUTPUT" ]]; then
        strip_file "$input" "$OPT_OUTPUT" || exit_status=3

    else
        strip_file "$input" "" || exit_status=3
    fi
done

exit $exit_status
