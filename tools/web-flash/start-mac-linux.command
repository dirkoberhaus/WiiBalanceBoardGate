#!/bin/bash
# Double-click this file (macOS/Linux) to start the local web-flash tool.
# WebSerial requires a real HTTP origin (even "localhost" counts as secure),
# so the page can't just be opened as a file:// - this starts a throwaway
# local server for it instead.
cd "$(dirname "$0")"
PORT=8080

if command -v python3 >/dev/null 2>&1; then
  PY=python3
elif command -v python >/dev/null 2>&1; then
  PY=python
else
  echo "Python wurde nicht gefunden. Bitte Python 3 installieren (https://python.org) und diese Datei erneut starten."
  read -p "Enter zum Beenden..."
  exit 1
fi

( sleep 1
  if command -v open >/dev/null 2>&1; then
    open "http://localhost:$PORT/"
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "http://localhost:$PORT/"
  fi
) &

echo "Server läuft - Browser öffnet sich gleich automatisch."
echo "Zum Beenden dieses Fenster schließen oder Strg+C drücken."
"$PY" -m http.server "$PORT" --bind 127.0.0.1
