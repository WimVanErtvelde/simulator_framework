@echo off
:: ============================================================================
:: RadioNav — Windows build script (MSVC)
::
:: HOW TO USE:
::   1. Open "Developer Command Prompt for VS 2022" (or 2019/2017)
::      Start → Visual Studio → Developer Command Prompt
::   2. cd to RadioNav\src\
::   3. Run:  build_windows.bat
::
:: Output: radionav.exe  (copy to RadioNav\ to use default data/ paths)
:: ============================================================================

setlocal

echo.
echo === RadioNav Windows Build ===
echo.

:: Compiler flags
::   /std:c++17           — C++17 standard
::   /EHsc                — C++ exception handling
::   /O2                  — optimise for speed
::   /W3                  — warning level 3
::   /nologo              — suppress banner
::   /utf-8               — treat source as UTF-8
::   /D_CRT_SECURE_NO_WARNINGS  — suppress fopen/strncpy deprecation warnings
::                                 (pre-existing in A424Parser; not our code)
set CFLAGS=/std:c++17 /EHsc /O2 /W3 /nologo /utf-8 /D_CRT_SECURE_NO_WARNINGS

:: Source files (all .cpp in this directory)
set SOURCES=^
    A424Parser.cpp ^
    AbstractReceiver.cpp ^
    DME_Receiver.cpp ^
    ILS_GlideslopeReceiver.cpp ^
    ILS_LocalizerReceiver.cpp ^
    ILS_MarkerReceiver.cpp ^
    LatLon.cpp ^
    LOSChecker.cpp ^
    MagDec.cpp ^
    Model.cpp ^
    NDB_Receiver.cpp ^
    NavSimTask.cpp ^
    TerrainModel.cpp ^
    TileGrid.cpp ^
    Units.cpp ^
    VOR_Receiver.cpp ^
    World.cpp ^
    WorldParser.cpp ^
    main.cpp

echo Compiling...
cl %CFLAGS% %SOURCES% /Fe:radionav.exe /link

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build SUCCEEDED  —  radionav.exe
    echo.
    echo Copy radionav.exe to RadioNav\ then run from there:
    echo   radionav.exe --help
    echo   radionav.exe --test
    echo   radionav.exe --xp12 data\earth_nav.dat
) else (
    echo.
    echo Build FAILED  ^(error %ERRORLEVEL%^)
)

endlocal
