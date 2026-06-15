#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
dest="${HOME}/.vscode/extensions/bovnar-highlight"
rm -rf "$dest"
mkdir -p "$(dirname "$dest")"
cp -r bovnar-highlight "$dest"
