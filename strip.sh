#!/usr/bin/env bash
#
# Strip comments from every C source and header in place, PRESERVING each file's
# leading SPDX/copyright block.
#
# strip_comments.sh -i removes every comment and writes back with no backup, and
# those licence headers ARE comments -- so this used to delete the
# "SPDX-License-Identifier: MIT" and copyright notice from all 27 files that
# carry one, irreversibly and with the tree possibly dirty. The MIT licence
# requires the notice to be retained, so it is captured before stripping and
# restored afterwards.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    echo "usage: ./strip.sh [--force]   (refuses on a dirty tree without --force)"
    exit 0
fi

# The operation is in-place and unrecoverable except through git, so require a
# clean tree unless the caller insists.
if [[ "${1:-}" != "--force" ]] && ! git diff --quiet HEAD 2>/dev/null; then
    echo "strip.sh: the working tree has uncommitted changes." >&2
    echo "          This rewrites every file in src/ and include/ in place with" >&2
    echo "          no backup. Commit or stash first, or pass --force." >&2
    exit 2
fi

strip_one() {
    local f="$1" hdr
    # The leading block comment, if it carries an SPDX tag.
    hdr="$(awk '
        NR==1 && $0 !~ /^\/\*/ { exit }
        { print }
        /\*\// && NR>1 { exit }
    ' "$f")"
    if [[ "$hdr" != *SPDX-License-Identifier* ]]; then
        hdr=""
    fi
    ./strip_comments.sh -i "$f"
    if [[ -n "$hdr" ]]; then
        printf '%s\n' "$hdr" | cat - "$f" > "$f.tmp$$" && mv -- "$f.tmp$$" "$f"
    fi
}

count=0
while IFS= read -r -d '' f; do
    strip_one "$f"
    count=$((count + 1))
done < <(find src include \( -iname "*.c" -o -iname "*.h" \) -print0)
echo "stripped $count file(s), licence headers retained"
