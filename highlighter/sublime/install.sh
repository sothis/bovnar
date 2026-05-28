#!/bin/bash
# Install the Bovnar syntax definition and colour scheme for Sublime Text.
set -e
cd "$(dirname "$0")"

# Locate the Sublime Text user Packages directory (ST4, then ST3 / macOS).
for base in \
    "$HOME/.config/sublime-text/Packages" \
    "$HOME/.config/sublime-text-3/Packages" \
    "$HOME/Library/Application Support/Sublime Text/Packages" \
    "$HOME/Library/Application Support/Sublime Text 3/Packages"; do
    if [ -d "$base" ]; then
        dest="$base/Bovnar"
        break
    fi
done

# Default to the Sublime Text 4 location if none exist yet.
: "${dest:=$HOME/.config/sublime-text/Packages/Bovnar}"

mkdir -p "$dest"
cp Bovnar.sublime-syntax "$dest"/
cp Bovnar-Cyberpunk.sublime-color-scheme "$dest"/

echo "Installed Bovnar Sublime Text package to: $dest"
echo "Activate via: Preferences > Select Color Scheme... > Bovnar Cyberpunk"
