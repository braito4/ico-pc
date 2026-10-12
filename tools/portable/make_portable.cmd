@echo off
rem make_portable.cmd: turns on portable mode for this copy of ICO. Saves,
rem settings and the game's data are then kept in the folder userdata beside
rem ico_pc_x64.exe instead of in %APPDATA%\ico-pc\ico-pc. Double-click it.
rem Paths are read with ! inside blocks, so a ) in a folder name is safe.
rem No labels or goto: the repository stores this file with LF line ends,
rem and the package converts it to CRLF.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "UD=%~dp0userdata"
set "PU=%APPDATA%\ico-pc\ico-pc"
echo Make this copy of ICO portable.
echo.
tasklist /FI "IMAGENAME eq ico_pc_x64.exe" /NH 2>nul | find /I "ico_pc_x64.exe" >nul
if not errorlevel 1 (
    echo ICO is running. Close it and run this again. Nothing was changed.
    pause
    exit /b 1
)
if exist "%UD%" (
    dir /b "%UD%" 2>nul | findstr "^" >nul
    if not errorlevel 1 (
        echo The folder userdata beside the program is not empty, so nothing was changed.
        echo If it holds your files, this copy is already portable.
        pause
        exit /b 1
    )
)
if not exist "%UD%" mkdir "%UD%"
if not exist "%UD%\" (
    echo Could not make the folder !UD!. Nothing was changed.
    echo Copy the game to a folder you can write to, such as one on your Desktop.
    pause
    exit /b 1
)
echo Made the folder userdata beside the program.
if exist "%PU%\" (
    echo.
    echo Your saves and settings are now in !PU!
    set "ANSWER="
    set /p "ANSWER=Move them into userdata? [y/N] "
    if /i "!ANSWER!"=="y" (
        robocopy "!PU!" "!UD!" /E /MOVE /NFL /NDL /NJH /NJS /NP >nul
        if errorlevel 8 (
            echo Moving the files failed. Copy them by hand from !PU! into !UD!
        ) else (
            echo Moved. Your saves and settings are in userdata now.
        )
    ) else (
        echo Left them there. The game starts fresh in userdata. To use them, close the game and copy
        echo everything from !PU! into !UD!
    )
)
findstr /b /i /c:"portable=0" "%~dp0ico-pc.ini" >nul 2>&1
if not errorlevel 1 (
    echo.
    echo Note: ico-pc.ini has the line portable=0, which turns portable mode off even
    echo with a userdata folder. Delete that line to use portable mode.
)
echo.
echo Done. To go back, put the line portable=0 in ico-pc.ini beside the program.
pause
