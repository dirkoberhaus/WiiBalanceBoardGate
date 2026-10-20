# Wii Balance Board Gate — Kurzanleitung

So verbindest du dich mit dem Balance Board und siehst die Gewichtsverteilung
live auf deinem Handy oder Laptop.

## 1. Board einschalten

Das kleine ESP32-Board per USB mit Strom versorgen (Netzteil, Powerbank oder
Laptop). Nach ein paar Sekunden ist es startbereit.

## 2. Balance Board verbinden — zuerst!

Unter der Batterieklappe des Wii Balance Boards den roten **SYNC**-Knopf
drücken. Das Board sucht dann automatisch nach der Verbindung — das kann
**ein paar Sekunden bis zu einer halben Minute** dauern und braucht manchmal
mehr als einen Versuch. Das ist normal (Bluetooth muss die Geräte erst
"finden") und **kein Fehler** — falls es nach dem ersten Drücken nicht sofort
klappt, den Knopf einfach nochmal drücken.

Das musst du **jedes Mal neu machen**, wenn das Balance Board sich
ausgeschaltet hat oder das Gateway-Board neu gestartet wurde — eine
dauerhafte Kopplung gibt es bei diesem Board nicht.

**Wichtig: diesen Schritt zuerst erledigen, bevor du dich mit dem WLAN
verbindest** (siehe nächster Schritt) — solange das Board noch nach dem
Balance Board sucht, blockiert das den WLAN-Funk so stark, dass sich kein
Handy/Laptop zuverlässig verbinden kann.

## 3. Mit dem WLAN verbinden

Jedes Board hat sein eigenes WLAN-Netz, z.B. `WiiBalanceBoardGate-XXXXXX`.
Das Netz ist zwar schon ab dem Einschalten sichtbar, aber der eigentliche
Verbindungsaufbau klappt erst zuverlässig, **nachdem** das Balance Board
verbunden ist (siehe oben).

Am einfachsten geht's über den **QR-Code, der auf dem Board klebt** — Handy-
Kamera draufhalten, antippen, fertig. Alternativ manuell verbinden:

- Netzwerk: `WiiBalanceBoardGate-XXXXXX` (Name steht auf dem Board)
- Passwort: `balanceboard`

Direkt nach dem Verbinden öffnet sich auf den meisten Handys automatisch
ein Browser-Fenster mit der Anzeige (iOS/Android erkennen das Board als
"Netzwerk mit Anmeldeseite" und zeigen sie von selbst) — dann ist Schritt 4
schon erledigt.

## 4. Seite öffnen (falls sie sich nicht von selbst geöffnet hat)

Im Browser eine beliebige Adresse eingeben, z.B. `http://board/` (oder
`192.168.4.1`, falls das nicht klappt). Es öffnet sich automatisch die
Anzeige.

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
- Unter dem Diagramm zeigt **"Vorfuß / Ferse: X% / Y%"** live die aktuelle
  Gewichtsverteilung auf dieser Achse an — nicht nur die Markierung im
  Diagramm, sondern die genaue Prozentzahl.
- **Bogensport-Ziel (60% Vorfuß / 40% Ferse):** Knopf "🏹 Ziel: 60% Vorfuß
  / 40% Ferse" aktiviert automatisch die für den Bogensport übliche
  Gewichtsverteilung als Zielpunkt, statt ihn manuell im Diagramm antippen
  zu müssen. Erscheint die Markierung dabei an der falschen Stelle (z.B.
  Richtung Ferse statt Vorfuß), liegt das daran, dass das Board andersherum
  unter den Füßen liegt als vom Gerät angenommen — dann einmal das Häkchen
  "Vorne/Hinten vertauscht" daneben setzen, danach passt die Markierung
  dauerhaft für dieses Board/diese Standposition. Einfach ins Diagramm
  tippen, um stattdessen wieder einen eigenen Zielpunkt zu setzen.

## Firmware aktualisieren (optional)

Dafür brauchst du kurz einen Computer (Windows/Mac/Linux) mit **Google
Chrome, Microsoft Edge oder Opera** (Firefox/Safari/Handy-Browser gehen
nicht) und ein USB-C-Kabel — nicht das WLAN des Boards.

1. Board per USB-C mit dem Computer verbinden.
2. Im Browser öffnen: **https://dirkoberhaus.github.io/WiiBalanceBoardGate/flash/**
3. Neben deinem Board-Modell (Heltec / TTGO / Mini D1) auf
   **"Installieren"** tippen.
4. Im Auswahlfenster den seriellen Port deines Boards auswählen und
   "Verbinden" bestätigen.
5. Warten, bis der Fortschrittsbalken bei 100% ist (ca. 1-2 Minuten). Das
   Board währenddessen nicht vom Kabel trennen.
6. Fertig — Board vom Computer trennen und wie gewohnt mit Strom versorgen.

Das Board merkt sich automatisch, ob die neue Firmware tatsächlich
hochgefahren ist — tut sie das nicht, springt es von selbst auf die
vorherige, funktionierende Version zurück.

## Problembehandlung

| Problem | Was tun |
|---|---|
| Kein WLAN-Netz sichtbar | Board hat evtl. keinen Strom — Kabel/Netzteil prüfen |
| Balance Board verbindet nicht | SYNC-Knopf nochmal drücken, ein paar Sekunden warten (siehe Schritt 2) |
| WLAN ist sichtbar, aber Handy/Laptop kommt nicht rein | Erst das Balance Board verbinden (Schritt 2), danach nochmal mit dem WLAN verbinden |
| Seite lädt nicht | Sicherstellen, dass du wirklich mit dem `WiiBalanceBoardGate-...`-Netz verbunden bist, nicht mit einem anderen WLAN |
| Gesamtgewicht auf der Webseite zeigt "uncalibrated" | Seltener Fehlerfall beim Auslesen der Kalibrierung — Balance Board neu verbinden (Batterie raus/rein, dann SYNC erneut drücken) |
