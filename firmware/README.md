# Firmware

PlatformIO-/Arduino-Firmware für ESP32-C3 SuperMini und ein rundes GC9A01A-Display mit 240 × 240 Pixeln. Verdrahtung und mechanischer Aufbau: [HARDWARE.md](../HARDWARE.md).

## Bauen und flashen

Im Ordner `firmware` ausführen:

```sh
# Lokale Anzeige mit zwei fiktiven Limits; kein WLAN und kein HTTP
pio run -e demo
pio run -e demo -t upload --upload-port <SERIELLER-PORT>

# WLAN-Gerät; Konfiguration anschließend mit dem Handy
pio run -e device
pio run -e device -t upload --upload-port <SERIELLER-PORT>
```

`demo` ist die Standardumgebung. Für das echte Gerät immer ausdrücklich `-e device` angeben. `config.h` ist für die normale Einrichtung nicht nötig. Optionale Initialwerte aus `include/config.example.h` sollten keine Zugangsdaten enthalten und werden von später gespeicherten Geräteeinstellungen überlagert.

## Anzeige

Die Firmware akzeptiert höchstens zwei Limits.

- Bei einem Limit erscheint ein großer Ring mit Prozentwert und Reset-Countdown. Darunter läuft die Laptop-/Terminalanimation; Uhrzeit und Datum stehen darüber.
- Bei zwei Limits erscheinen dauerhaft zwei feste konzentrische Ringe. `AUSSEN` bezeichnet das erste, `INNEN` das zweite Limit. Die Animation ist in dieser Ansicht nicht aktiv.
- Uhrzeit und Datum verwenden die konfigurierte lokale Zeitzone aus `include/hardware_config.h`. Nach einer SNTP-Synchronisation zeigt die Firmware `HH:MM TT.MM.`; ohne gültige Zeit Platzhalter.
- Grün gilt ab 45 %, Gelb ab 20 %, darunter Rot. Demo-Daten, fehlende Daten, veraltete Daten, WLAN-Ausfall und Abruffehler sind sichtbar gekennzeichnet.

Im öffentlichen Paket ist die Laptop-/Terminalgrafik direkt im C++-Code gezeichnet; dafür sind keine Bilddateien nötig. Eine private Entwicklungsfassung kann eine vorhandene Rocky-Sprite-Datei weiterverwenden. Diese Datei wird wegen ungeklärter Weiterverteilungsrechte nicht mit dem öffentlichen Paket ausgeliefert. Details: [ASSETS.md](../ASSETS.md).

## WLAN und Dienst

Die `device`-Firmware öffnet beim Erststart ein passwortgeschütztes Einrichtungs-WLAN. Dort werden WLAN, private Server-IP und Port eingetragen; `/v1/quota` ergänzt die Firmware. Anleitung: [WLAN-EINRICHTUNG.md](../WLAN-EINRICHTUNG.md).

Das Gerät ruft den Dienst standardmäßig alle 300 Sekunden ab. Nach Transport-, HTTP- oder JSON-Fehlern versucht es nach 30 Sekunden erneut. WLAN-Wiederverbindung wird unabhängig davon angestoßen. Antworten sind auf 4096 Byte begrenzt; Connect- und Lese-Timeout liegen bei fünf Sekunden.

Der letzte gültige Stand bleibt bei Fehlern sichtbar, wird aber als `LOST`, `OFFLINE` oder `STALE` markiert. Der Reset-Countdown läuft zwischen Antworten lokal weiter; bei Ablauf werden keine neuen Prozentwerte erfunden.

## Datenvertrag

Die Firmware erwartet `GET /v1/quota` mit JSON-Schema-Version 1. Sie prüft Kernfelder, Wertebereiche und höchstens zwei Limits strikt. Die vollständige Beschreibung und ein Beispiel stehen in [JSON.md](../JSON.md).

## Hardware- und Funktionsprüfung

Vor dem ersten Einschalten die Abnahme in [HARDWARE.md](../HARDWARE.md#abnahme-vor-dem-ersten-flashen) durchführen. Anschließend mindestens prüfen:

```sh
pio run -e demo
pio run -e device
```

Am Gerät Demo-Anzeige, WLAN-Ersteinrichtung, erneutes Öffnen mit BOOT, Erreichbarkeit des Diensts sowie Anzeige bei WLAN- und Dienstverlust testen. Dokumentierte automatisierte und manuelle Prüfungen: [TESTS.md](../TESTS.md).

## Falls der normale Upload mit dem Stub scheitert

Beim Referenz-SuperMini war der ROM-Upload mit `--no-stub` zuverlässig. Nach
`pio run -e device` kann das esptool aus der PlatformIO-Installation direkt benutzt
werden (Port und Pfade an das eigene System anpassen, Flashgröße vorher prüfen):

```sh
python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32c3 --port /dev/ttyACM0 --baud 115200 --no-stub \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode keep --flash_freq keep --flash_size 4MB \
  0x0000 .pio/build/device/bootloader.bin \
  0x8000 .pio/build/device/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/device/firmware.bin
```

Dieser Upload erhält vorhandene NVS-Einstellungen. Zum Weitergeben eines Geräts
NVS ausdrücklich löschen; ein gewöhnliches Update reicht dafür nicht aus.

## Gespeicherte Zurücksetzungen

Bei einem Limit zeigt die Anzeige links neben der Animation je eine goldene
Sternmarke pro verfügbarer Zurücksetzung (höchstens drei, weitere als `+N`).
Bei zwei Limits steht der Zähler kompakt oberhalb der Ringe. `R?` bzw. `RESETS ?`
bedeutet unbekannt, `R:0` bedeutet bestätigte null. Bei veralteten Daten oder
Verbindungsfehlern wird die Markierung grau. Quelle ist ausschließlich der
optionale Dienstwert `reset_credits`; die Anzeige löst keine Zurücksetzung aus.
