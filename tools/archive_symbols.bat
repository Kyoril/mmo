@echo off
REM Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
REM
REM Archives the current build's PDB (and binary) into the local symbol store so crash reports can
REM be symbolicated later. Forwards any arguments to archive_symbols.ps1, e.g.:
REM
REM     tools\archive_symbols.bat
REM     tools\archive_symbols.bat -Binary bin\Release\mmo_client.exe -Store F:\mmo-symbols
REM     tools\archive_symbols.bat -NoBinary
REM
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0archive_symbols.ps1" %*
if errorlevel 1 (
    echo.
    echo Symbol archiving FAILED.
    exit /b 1
)
