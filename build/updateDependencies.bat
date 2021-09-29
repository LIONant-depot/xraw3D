@echo OFF
setlocal enabledelayedexpansion
set XRAW3D_PATH=%cd%

rem --------------------------------------------------------------------------------------------------------
rem Set the color of the terminal to blue with yellow text
rem --------------------------------------------------------------------------------------------------------
COLOR 8E
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore Cyan Welcome I am your XRAW 3D dependency updater bot, let me get to work...
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

:DOWNLOAD_DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XRAW 3D - DOWNLOADING DEPENDENCIES
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
echo.

rem ------------------------------------------------------------
rem XCORE
rem ------------------------------------------------------------
:XCORE
rmdir "../dependencies/xcore" /S /Q
git clone https://gitlab.com/LIONant/xcore.git "../dependencies/xcore"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd ../dependencies/xcore/builds
if %ERRORLEVEL% GEQ 1 goto :ERROR
call UpdateDependencies.bat "return"
if %ERRORLEVEL% GEQ 1 goto :ERROR
cd /d %XRAW3D_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem ASSIMP
rem ------------------------------------------------------------
:ASSIMP
rmdir "../dependencies/assimp" /S /Q
git clone https://github.com/assimp/assimp.git "../dependencies/assimp"
if %ERRORLEVEL% GEQ 1 goto :ERROR

cd ../dependencies/assimp
if %ERRORLEVEL% GEQ 1 goto :ERROR

cmake CMakeLists.txt -G "Visual Studio 16 2019" -S . -B ./BINARIES/Win32
if %ERRORLEVEL% GEQ 1 goto :ERROR

cmake --build ./BINARIES/Win32 --config release
if %ERRORLEVEL% GEQ 1 goto :ERROR

cd /d %XRAW3D_PATH%
if %ERRORLEVEL% GEQ 1 goto :ERROR

rem ------------------------------------------------------------
rem DONE
rem ------------------------------------------------------------
:DONE
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
powershell write-host -fore White XRAW 3D - DONE!!
powershell write-host -fore White ------------------------------------------------------------------------------------------------------
goto :PAUSE

:ERROR
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------
powershell write-host -fore Red XRAW 3D - ERROR!!
powershell write-host -fore Red ------------------------------------------------------------------------------------------------------

:PAUSE
rem if no one give us any parameters then we will pause it at the end, else we are assuming that another batch file called us
if %1.==. pause