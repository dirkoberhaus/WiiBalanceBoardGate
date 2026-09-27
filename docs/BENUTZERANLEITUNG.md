# Wii Balance Board Gate — Kurzanleitung

So verbindest du dich mit dem Balance Board und siehst die Gewichtsverteilung
live auf deinem Handy oder Laptop.

## 1. Board einschalten

Das kleine ESP32-Board per USB mit Strom versorgen (Netzteil, Powerbank oder
Laptop). Nach ein paar Sekunden ist es startbereit.

## 2. Mit dem WLAN verbinden

Jedes Board hat sein eigenes WLAN-Netz, z.B. `WiiBalanceBoardGate-XXXXXX`.
Am einfachsten geht's über den **QR-Code, der auf dem Board klebt** — Handy-
Kamera draufhalten, antippen, fertig. Alternativ manuell verbinden:

- Netzwerk: `WiiBalanceBoardGate-XXXXXX` (Name steht auf dem Board)
- Passwort: `balanceboard`

## 3. Seite öffnen

Im Browser eine beliebige Adresse eingeben, z.B. `http://board/` (oder
`192.168.4.1`, falls das nicht klappt). Es öffnet sich automatisch die
Anzeige.

## 4. Balance Board verbinden

Unter der Batterieklappe des Wii Balance Boards den roten **SYNC**-Knopf
drücken. Das Board sucht dann automatisch nach der Verbindung — das kann
**ein paar Sekunden bis zu einer halben Minute** dauern und braucht manchmal
mehr als einen Versuch. Das ist normal (Bluetooth muss die Geräte erst
"finden") und **kein Fehler** — falls es nach dem ersten Drücken nicht sofort
klappt, den Knopf einfach nochmal drücken.

Das musst du **jedes Mal neu machen**, wenn das Balance Board sich
ausgeschaltet hat oder das Gateway-Board neu gestartet wurde — eine
dauerhafte Kopplung gibt es bei diesem Board nicht.

## 5. Benutzen

- Draufstellen zeigt die Gewichtsverteilung als Punkt in der Mitte des
  Displays (bzw. auf der Webseite) — je weiter der Punkt aus der Mitte
  wandert, desto mehr Gewicht liegt auf der jeweiligen Seite.
- **Tarieren** (auf 0 zurücksetzen): passiert automatisch beim Verbinden.
  Boards mit Taster (Heltec/TTGO) können den zusätzlich jederzeit per
  Knopfdruck erneut tarieren.
- Aus Datenschutzgründen wird das tatsächliche Gewicht in kg **nicht**
  direkt angezeigt — nur die relative Verteilung. Auf der Webseite lassen
  sich die genauen Zahlen bei Bedarf über einen "Details anzeigen"-Schalter
  einblenden.

## Problembehandlung

| Problem | Was tun |
|---|---|
| Kein WLAN-Netz sichtbar | Board hat evtl. keinen Strom — Kabel/Netzteil prüfen |
| Balance Board verbindet nicht | SYNC-Knopf nochmal drücken, ein paar Sekunden warten (siehe Schritt 4) |
| Seite lädt nicht | Sicherstellen, dass du wirklich mit dem `WiiBalanceBoardGate-...`-Netz verbunden bist, nicht mit einem anderen WLAN |
| Anzeige bleibt bei "UNCALIBRATED" | Balance Board neu verbinden (Batterie raus/rein, dann SYNC erneut drücken) |
