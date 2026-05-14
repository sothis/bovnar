#!/usr/bin/env bash
find src \( -iname "*.c" -o -iname "*.h" \) -exec ./strip_comments.sh -i {} +
find include \( -iname "*.c" -o -iname "*.h" \) -exec ./strip_comments.sh -i {} +
