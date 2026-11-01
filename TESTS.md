# Prüfung und bekannte Grenzen

## Zusätzliche Zurücksetzungen (01.11.2026)

Die private Device-Firmware wurde mit 62.692 B statischem RAM und 1.268.240 B
Flash erfolgreich kompiliert und gelinkt. Wegen lokaler Toolchain-/SCons-Probleme
wurde der letzte `elf2image`-Schritt direkt mit den aus dem PlatformIO-Builder
geprüften Parametern ausgeführt; esptool bestätigte Image-Prüfsumme und SHA-Hash.
Das Image (1.309.536 B) passt knapp in die 1.310.720-B-App-Partition.
USB-Upload auf dem Referenzgerät mit Hash-Prüfung erfolgreich. Die Sternanordnung
muss optisch am Gerät begutachtet werden; ein Foto der neuen Anzeige liegt noch nicht vor.

26 Diensttests und vier Portaltests bestanden. Die festgelegte Codex-CLI
`0.164.0-alpha.11.1` unterstützt `rateLimitResetCredits.availableCount` sowohl
im generierten Schema als auch bei einer echten, ausschließlich lesenden Abfrage.
Der aktualisierte Referenz-Container liefert den neuen optionalen JSON-Wert.
Ein unbekannter Wert wird von bestätigten 0 unterschieden; Details/IDs werden nicht weitergereicht.

## Öffentliche Ausgabe geprüft (31.10.2026)

Direkt aus dem bereinigten ZIP und ohne Rocky-Sprite kompiliert:

| Profil | Statischer RAM | Flash | Ergebnis |
| --- | ---: | ---: | --- |
| device | 62.604 B | 975.754 B | erfolgreich |
| demo | 41.856 B | 403.308 B | erfolgreich |

24 Diensttests, 4 Portaltests und alle vier C++-Hostprogramme bestanden
(darunter 101 Provisioning-Validierungen). ZIP-Dateiliste, bekannte private
Kennungen, gängige Secret-Muster und lokale Dokumentverweise geprüft.
Die öffentliche Laptopgrafik wurde kompiliert, aber noch nicht am Display begutachtet.
Bestehende ArduinoJson-Deprecation-Warnungen verhindern den Build nicht.

## Automatisch reproduzierbar

Node.js 22 oder neuer, im Projektordner:

```sh
npm test --prefix service
node --test firmware/test/web/*.test.mjs
```

Die Tests prüfen Quoten-Normalisierung, Fehler-/Veraltet-Zustände, Dashboard,
Authentifizierung und das tatsächliche eingebettete Portal-JavaScript.

Die vier unabhängigen C++-Hosttests lassen sich mit einem C++11-Compiler ausführen:

```sh
for name in clock_text pet_motion pet_centering provisioning_validation; do
  c++ -std=c++11 -Wall -Wextra -pedantic -Ifirmware/include \
    firmware/test/host/${name}_test.cpp -o /tmp/gauge-${name}
  /tmp/gauge-${name} || exit 1
done
```

Firmware:

```sh
cd firmware
pio run -e demo
pio run -e device
```

GitHub Actions prüft zusätzlich den tatsächlichen Linux-Docker-Build, CA-Store,
konfigurierbaren Host-Port und den Nginx-Zugriffsschutz mit kurzlebigem Testschlüssel.
`deploy/test-remote.sh` ausschließlich in einer separaten Testumgebung verwenden:
es löscht seine Compose-Testvolumes. Niemals am produktiven Stack ausführen.

## Praktisch geprüfter Referenzaufbau

ESP32-C3 SuperMini mit 4 MB Flash und Sieben-Pin-GC9A01-Modul: Display, geglättete
Ringe, Animation, Handy-Einrichtung, WLAN-Suche, persistente Einstellungen und
zertifikatsgeprüfter HTTPS-Abruf wurden praktisch geprüft. Zuletzt ca. 63 KB
statischer RAM und 1,26 MB Firmware; Flashreserve ist klein.
Uhr und Datum nutzen eine zentrierte gemeinsame Zeile. Ein Limit zeigt eine
Animation; zwei Limits erscheinen als feste Ringe.

## Selbst am eigenen Gerät prüfen

Modulversorgung und Pinbelegung, USB-/Boot-Verhalten, Antennenfreiraum,
Displayausrichtung und Gehäusepassung hängen vom konkreten Klon ab.
Kontofreigaben, Unternehmens-WLAN, NTP/DNS und TLS-Inspektion sind installationsabhängig.
Nach jeder Einrichtung die Kennzeichnung DEMO, erfolgreiche Aktualisierung,
WLAN-Ausfall und Veraltet-Zustand prüfen. Reset erzeugt niemals erfundene 100 %.
