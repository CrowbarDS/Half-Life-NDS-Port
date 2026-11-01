#!/bin/sh
# fetch.sh <repository> <commit> <folder>: that commit and nothing else.
set -eu
git init -q "$3"
cd "$3"
git remote add origin "$1"
git fetch -q --depth 1 origin "$2"
git checkout -q FETCH_HEAD
