# Saubere Weitergabe

Das Quellpaket enthält ausschließlich Projektdateien. Nicht weitergeben:
`.env`, `config.h`, `secrets/`, Codex-Auth-Volumes, Build-Verzeichnisse,
serielle Logs, private Fotos oder Firmware mit eingebrannten Zugangsdaten.
Ein fertig eingerichtetes Gerät enthält WLAN und gegebenenfalls den Server-Schlüssel
im unverschlüsselten NVS. Vor Weitergabe Flash vollständig löschen und neu flashen;
dabei gehen auch die eigenen Einstellungen verloren.

Domain `gauge.example.org` und LAN-Adresse `192.168.1.50` sind Beispiele.
Die eigene Domain, Adresse und Ports werden erst bei der Einrichtung eingetragen.
Das mitgelieferte ZIP hat keine Git-Historie. Eine frühere private Entwicklungs-
historie ist nicht zur Veröffentlichung vorgesehen. Für ein öffentliches Repository
mit dem ZIP in einem neuen, leeren Repository beginnen; keine alte Historie pushen.

## Herkunft und Nutzungsrechte

Dies ist ein unabhängiges Hobbyprojekt, kein offizielles OpenAI-Produkt.
Abhängigkeiten unterliegen ihren jeweiligen Lizenzen (Adafruit, Arduino/ESP32,
ArduinoJson, Node.js und Codex CLI). Installierte Abhängigkeiten sind nicht im Paket.

Die Rocky-Sprite-Daten in `firmware/include/rocky_typing.h` stammen vom eingebauten
OpenAI-Pet, nicht aus einer eigenen Illustration. Eine allgemeine Lizenz zur
Weiterverbreitung dieser Grafik ist nicht nachgewiesen. Das Projekt erteilt dafür
keine Lizenz. Vor öffentlicher Verteilung diese Datei durch eigene oder ausdrücklich
freigegebene, kompatible Sprite-Daten ersetzen. Die private Entwicklungsfassung
enthält sie zur Erhaltung des bestehenden Aufbaus; das öffentliche Quellpaket
enthält sie ausdrücklich nicht. Siehe Anleitung im Paket unter `ASSETS.md`.

3D-Druckdateien werden verlinkt, nicht übernommen. Lizenz und Druckhinweise beim
jeweiligen Originalprojekt beachten.
