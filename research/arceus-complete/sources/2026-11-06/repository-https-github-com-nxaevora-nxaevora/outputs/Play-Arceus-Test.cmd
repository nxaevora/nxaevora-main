@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright 2026 the NXAevora contributors
setlocal
set "APPDATA=C:\Users\brett\Documents\Codex\work\arceus-appdata"
set "PATH=C:\Users\brett\Documents\Codex\work\msys64\clang64\bin;C:\Users\brett\Documents\Codex\work\llvm-mingw-20261104-ucrt-x86_64\bin;%PATH%"
cd /d "C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest"
start "" "C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest\ArceusTest.exe" --nvn_native=0 --fullscreen=0 --log=1
