#!/usr/bin/env zsh

cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy

source ~/.zshrc;
source .envrc
./output/x64/Debug/QtScrcpy 2>&1 | tee -a output.log
