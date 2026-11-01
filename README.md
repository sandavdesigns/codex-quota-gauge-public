# Codex Quota Gauge

Ein kleines WLAN-Display mit ESP32-C3 und rundem GC9A01A-Display (240 × 240 Pixel). Ein lokaler Dienst liest das Codex-Nutzungskontingent eines ChatGPT-Abos (keine API-Kosten) und stellt es bereit; das Gerät zeigt sie im privaten Netz an. Das Projekt ist als nachbaubare Vorlage gedacht: Zugangsdaten, Serveradressen und Konten gehören nicht ins Repository.

## Was die Anzeige kann

- Ein geliefertes Limit: großer Ring, Prozentwert, Reset-Countdown, Uhrzeit/Datum und eine animierte Laptop-/Terminalgrafik.
- Zwei gelieferte Limits: zwei feste, konzentrische Ringe. Der äußere Ring ist als `AUSSEN`, der innere als `INNEN` beschriftet; die Animation ist dann nicht sichtbar.
- Verfügbare zusätzliche Zurücksetzungen als goldene Sterne (Einzelansicht); unbekannte oder veraltete Werte klar gekennzeichnet.
- Uhrzeit und Datum im Format `HH:MM TT.MM.`; ohne erfolgreiche Zeitsynchronisation erscheinen Platzhalter.
- Deutlich markierte Demo-, Offline-, Fehler- und veraltete Zustände.

Der Dienst und die Firmware verarbeiten höchstens zwei Limits. Die lokale Demo liefert zwei fiktive Werte und baut keine WLAN-Verbindung auf.

## Überblick

```text
angemeldete Codex-CLI auf einem Server
                │
                ▼
lokaler Dienst: Dashboard und /v1/quota
                │ privates LAN
                ▼
ESP32-C3 ── SPI ── GC9A01A
```

Die Anmeldung bleibt auf dem Server. Der ESP32 speichert nur seine WLAN-Einrichtung und die Adresse des lokalen Dienstes. `/v1/quota` verwendet im Standardaufbau HTTP ohne eigene Anmeldung. Deshalb: nur im vertrauenswürdigen privaten Netz betreiben, keine Router-Portweiterleitung einrichten und keine Tokens in Firmware, `.env` oder URLs ablegen.

Die tatsächliche Datenquelle und ihre Grenzen: [DATENQUELLE.md](DATENQUELLE.md). Eigene Server-Anmeldung erforderlich; verfügbare Zeitfenster sind kontoabhängig.

## Download und Installation

```sh
git clone https://github.com/sandavdesigns/codex-quota-gauge-public.git
cd codex-quota-gauge-public
```

Alternativ [ZIP herunterladen](https://github.com/sandavdesigns/codex-quota-gauge-public/archive/refs/heads/main.zip).
Docker-/Portainer-Einrichtung: [DOCKER.md](DOCKER.md).

## Schnellstart

1. [HARDWARE.md](HARDWARE.md) lesen und die Verdrahtung stromlos prüfen.
2. Den Dienst zunächst mit Demo-Daten starten: [DOCKER.md](DOCKER.md).
3. Die Firmware als Demo bauen und flashen:

   ```sh
   cd firmware
   pio run -e demo -t upload
   ```

4. Für das echte Gerät die WLAN-Variante flashen und am Handy einrichten:

   ```sh
   pio run -e device -t upload
   ```

   Die Schritte stehen in [WLAN-EINRICHTUNG.md](WLAN-EINRICHTUNG.md). Für Build, Protokoll und Anzeige gibt es zusätzlich [firmware/README.md](firmware/README.md).

5. Erst wenn Demo, Netzwerkweg und Anzeige funktionieren, die Codex-CLI auf dem Server anmelden und den Dienst von `SOURCE=demo` auf `SOURCE=codex` umstellen.

## Projektstruktur

- `firmware/` – PlatformIO-/Arduino-Firmware für Demo und WLAN-Gerät.
- `service/` – Node.js-Dienst, JSON-Endpunkt und Dashboard.
- `compose.yaml`, `.env.example` – Docker-Start für den lokalen Dienst.
- [ASSETS.md](ASSETS.md) – Herkunft der öffentlichen Laptop-/Terminalgrafik und Abgrenzung zur privaten Entwicklungsfassung.
- [WEITERGABE.md](WEITERGABE.md) – Checkliste für eine sichere, neutrale Weitergabe.
- [REMOTE.md](REMOTE.md) – optional sicherer Zugriff über eigene HTTPS-Domain und Nginx Proxy Manager.
- [JSON.md](JSON.md) – Vertrag für `/v1/quota`.
- [TESTS.md](TESTS.md) – vorhandene Prüfungen und verbleibende Hardwaretests.

## Betriebshinweise

Der Dienst fragt die Quelle standardmäßig alle fünf Minuten ab. Das Display fragt `/v1/quota` ebenfalls alle fünf Minuten ab und versucht nach Fehlern früher erneut. Alte Werte bleiben sichtbar, sind aber als `STALE`, `LOST` oder `OFFLINE` gekennzeichnet. Ein abgelaufener Reset führt nicht zu künstlich erzeugten 100 %.

Die WLAN-Einrichtung liegt unverschlüsselt im Flash/NVS, sofern auf dem ESP32 keine Flash-Verschlüsselung aktiviert wurde. Vor Weitergabe des Geräts den Flash einschließlich NVS löschen und neu flashen. Details: [WLAN-EINRICHTUNG.md](WLAN-EINRICHTUNG.md#was-gespeichert-wird).

Das öffentliche Paket verwendet die direkt im C++-Code gezeichnete Laptop-/Terminalanimation. Das bestehende private Gerät kann seine dort vorhandene Rocky-Grafik unverändert weiterverwenden; diese Sprite-Datei gehört wegen ungeklärter Weiterverteilungsrechte nicht ins öffentliche Paket. Details: [ASSETS.md](ASSETS.md).

## Lizenz

Eigener Projektcode und Dokumentation: [MIT](LICENSE). Fremde Abhängigkeiten und Gehäusemodelle behalten ihre eigenen Bedingungen; siehe [WEITERGABE.md](WEITERGABE.md).
