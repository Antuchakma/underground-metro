@echo off
REM Builds and runs the metro station demo: bin\metro.exe
cd /d "%~dp0"
REM Find g++ even if this shell's PATH predates the compiler install
where g++ >nul 2>nul || for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do set "PATH=%%D\mingw64\bin;%PATH%"
g++ -std=c++17 -O2 src\metro\*.cpp -o bin\metro.exe -Iinclude -Llib -lglfw3 -lopengl32 -lgdi32 -luser32 -lshell32 -static-libgcc -static-libstdc++ -static
if errorlevel 1 exit /b 1
echo Built bin\metro.exe
start "" /D bin bin\metro.exe
