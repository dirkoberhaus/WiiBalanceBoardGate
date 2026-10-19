@echo off
REM Double-click this file (Windows) to start the local web-flash tool.
REM WebSerial requires a real HTTP origin (even "localhost" counts as
REM secure), so the page can't just be opened as a file:// - this starts a
REM throwaway local server for it instead.
cd /d "%~dp0"
set PORT=8080

where python >nul 2>nul
if %ERRORLEVEL%==0 (
  set PY=python
) else (
  where py >nul 2>nul
  if %ERRORLEVEL%==0 (
    set PY=py
  ) else (
    echo Python wurde nicht gefunden. Bitte Python 3 installieren ^(https://python.org^) und diese Datei erneut starten.
    pause
    exit /b 1
  )
)

start "" "http://localhost:%PORT%/"
echo Server laeuft - Browser oeffnet sich gleich automatisch.
echo Zum Beenden dieses Fenster schliessen oder Strg+C druecken.
%PY% -m http.server %PORT% --bind 127.0.0.1
pause
