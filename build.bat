@echo off
REM Usage: build.bat src\file.cpp   -> builds bin\file.exe and runs it
REM Links against both freeglut and GLFW, so either kind of program works.
if "%~1"=="" (
    echo Usage: build.bat src\yourfile.cpp
    exit /b 1
)
cd /d "%~dp0"
REM Find g++ even if this shell's PATH predates the compiler install
where g++ >nul 2>nul || for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do set "PATH=%%D\mingw64\bin;%PATH%"
g++ "%~1" -o "bin\%~n1.exe" -Iinclude -Llib -lfreeglut -lglfw3 -lopengl32 -lglu32 -lgdi32 -luser32 -lwinmm -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
echo Built bin\%~n1.exe
"bin\%~n1.exe"
