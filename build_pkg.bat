@echo off
setlocal
title Slay the Spire PS3 - PKG builder
cd /d "%~dp0"
set "ROOT=%~dp0"
set "GAME_JAR=%ROOT%game\desktop-1.0.jar"
set "STEAM_JAR=C:\Program Files (x86)\Steam\steamapps\common\SlayTheSpire\desktop-1.0.jar"
set "JAR="

echo.
echo  Slay the Spire PS3 - PKG builder
echo  ---------------------------------
echo.

rem Step 1: find the game file. A dragged file wins, then the copy in game\, then the Steam folder.
if not "%~1"=="" set "JAR=%~1"
if defined JAR goto :check_name
if exist "%GAME_JAR%" set "JAR=%GAME_JAR%"
if defined JAR goto :check_name
if exist "%STEAM_JAR%" set "JAR=%STEAM_JAR%"
if defined JAR goto :check_name
goto :jar_missing

:jar_missing
echo I could not find desktop-1.0.jar.
echo.
echo To fix this:
echo  1. Drag desktop-1.0.jar onto build_pkg.bat.
echo  2. Or find it in Steam: right-click Slay the Spire, choose Manage,
echo     then Browse local files. Then drag that file onto build_pkg.bat.
echo.
pause
exit /b 1

:check_name
if not exist "%JAR%" goto :jar_missing
for %%F in ("%JAR%") do set "JAR_NAME=%%~nxF"
if /i "%JAR_NAME%"=="desktop-1.0.jar" goto :jar_ok
echo The file you picked is called "%JAR_NAME%".
echo The game file must be called desktop-1.0.jar.
echo.
echo Please drag desktop-1.0.jar onto build_pkg.bat.
echo.
pause
exit /b 1

:jar_ok
echo Using game file:
echo  %JAR%
echo.

rem Step 2: Docker must be installed and running.
echo Checking Docker Desktop...
docker info >nul 2>&1
if not errorlevel 1 goto :docker_ok
echo Docker Desktop is not running, or it is not installed.
echo.
echo Docker Desktop must be installed and running:
echo  1. Install it from https://www.docker.com/products/docker-desktop/
echo  2. Start it. Wait until it says Docker Desktop is running.
echo     Look for the whale icon in the system tray.
echo  3. Run build_pkg.bat again.
echo.
pause
exit /b 1

:docker_ok
rem Step 3: put a copy of the game file in game\ where the build expects it.
for %%F in ("%JAR%") do set "SOURCE_FULL=%%~fF"
for %%F in ("%GAME_JAR%") do set "TARGET_FULL=%%~fF"
if /i "%SOURCE_FULL%"=="%TARGET_FULL%" goto :jar_in_place
if not exist "%ROOT%game" mkdir "%ROOT%game"
copy /y "%JAR%" "%GAME_JAR%" >nul
if errorlevel 1 goto :copy_failed
echo Copied the game file into the game folder.
echo.
goto :jar_in_place

:copy_failed
echo Could not copy desktop-1.0.jar into the game folder.
echo Check that the file is not open in another program, then run
echo build_pkg.bat again.
echo.
pause
exit /b 1

:jar_in_place
rem Step 4: say what will happen and how long it takes.
echo What happens next:
echo  - The PS3 build tools are set up in Docker, and your game is converted.
echo  - Then the PKG file is built. This takes a long time.
echo  - First run: about 20 to 30 minutes. It downloads about 3 GB of tools.
echo  - Later runs: about 15 minutes.
echo  - You can use the computer for other things meanwhile.
echo  - Keep this window open until it says it is finished.
echo.
echo Starting the build now...
echo.

rem Step 5: run the build. Its own script does the Docker and PS3 work.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%ps3\build.ps1" -Prepare -Package
if not "%ERRORLEVEL%"=="0" goto :build_failed

rem Step 6: copy the finished PKG next to this file.
set "BUILT_PKG=%ROOT%build\ps3\slaythespire.pkg"
set "OUT_PKG=%ROOT%SlayTheSpire-PS3.pkg"
if not exist "%BUILT_PKG%" goto :pkg_missing
copy /y "%BUILT_PKG%" "%OUT_PKG%" >nul
if errorlevel 1 goto :pkg_copy_failed
for %%F in ("%OUT_PKG%") do set "OUT_FULL=%%~fF"

echo.
echo ==========================================
echo  Done! Your PKG is ready here:
echo  %OUT_FULL%
echo ==========================================
echo.
echo How to install it:
echo  RPCS3 (on a PC): File ^> Install Packages/Raps/Edats, then pick the PKG.
echo  PS3 (with HEN or CFW): copy the PKG to a USB stick. On the PS3,
echo  open Package Manager ^> Install Package Files, then pick the PKG.
echo.
pause
exit /b 0

:build_failed
echo.
echo The PKG build failed.
echo The messages above say why. Please keep this window open,
echo or take a screenshot of it, and ask for help.
echo.
pause
exit /b 1

:pkg_missing
echo.
echo The build finished, but the PKG file was not found at:
echo  %BUILT_PKG%
echo Please keep this window open, or take a screenshot of it, and ask for help.
echo.
pause
exit /b 1

:pkg_copy_failed
echo.
echo The PKG was built, but it could not be copied to this folder.
echo Check that no other program has SlayTheSpire-PS3.pkg open, then try again.
echo.
pause
exit /b 1
