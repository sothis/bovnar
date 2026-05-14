#!/bin/bash
git reset --hard
git clean -xdff
git gc --aggressive
git repack -Ad
git prune
git gc --aggressive
./merge.sh
