# Hardware: ESP32-C3 SuperMini und rundes GC9A01A-Display

Diese Anleitung gilt für ein siebenpoliges GC9A01A-Modul mit den Anschlüssen `VCC`, `GND`, `SCL`, `SDA`, `DC`, `CS` und `RST`. Andere Displayvarianten können andere Pinbelegungen, Abmessungen oder eine andere Versorgung benötigen.

## Gehäuse

Der Erbauer verwendete als Bauform den [Puck aus dem Projekt `display-case-gc9a01`](https://github.com/medpex/display-case-gc9a01/tree/main/puck). Vor einem Druck die Maße des eigenen Displays, des ESP32-C3-Boards, der USB-Buchse und der Kabel prüfen: GC9A01A-Module und SuperMini-Klone unterscheiden sich mechanisch. Druckeinstellungen und sonstige Angaben stehen im verlinkten Projekt.

Die Verkabelung und Firmware des fremden Gehäuseprojekts unterscheiden sich von diesem Projekt: für die Codex-Anzeige ausschließlich die folgende Tabelle verwenden. Fremde Web-Flasher würden eine andere Anwendung installieren.

## Verdrahtung

| Display-Aufdruck | ESP32-C3 | Aufgabe |
| --- | --- | --- |
| VCC | 3V3 | Versorgung des hier verwendeten 3,3-V-Aufbaus |
| GND | GND | gemeinsame Masse |
| SCL | GPIO4 | SPI-Takt |
| SDA | GPIO6 | SPI-MOSI |
| DC | GPIO1 | Daten/Befehl |
| CS | GPIO7 | Chip-Select |
| RST | GPIO0 | Display-Reset |

`SCL` und `SDA` sind bei diesem Modul SPI-Signale, nicht I²C. Die Zuordnung entspricht `firmware/include/hardware_config.h`. Es sind keine MISO- oder Backlight-Leitung vorgesehen. Alle ESP32-Signale arbeiten mit 3,3 V; niemals ein 5-V-Signal an einen GPIO legen.

GPIO2, GPIO8 und GPIO9 bleiben für das Display frei. GPIO9 wird für die BOOT-Taste genutzt. GPIO18/19 bleiben ebenfalls frei, damit USB-JTAG nicht umgewidmet wird. GPIO12–17 nicht für das Display verwenden, da sie typischerweise zum Flash gehören.

## Versorgung und Aufbau

- Stromlos löten; zuerst Pin-Aufdruck und Durchgang prüfen.
- Display und ESP32 über GND verbinden, dann VCC und erst danach die Signale.
- Die Versorgungsspannung des konkreten Displaymoduls anhand seiner Unterlagen prüfen. Diese Verdrahtung verwendet 3V3; eine Angabe zum Display-Controller allein ist keine Zusage für jedes fertige Modul.
- Kurze Leitungen verwenden, Zugentlastung vorsehen und die ESP32-Antenne freihalten.
- USB-C versorgt den Aufbau. Keine zweite Versorgung parallel einspeisen.

## Abnahme vor dem ersten Flashen

1. Stromlos VCC gegen GND und alle sieben Endpunkte auf Kurzschluss bzw. Durchgang prüfen.
2. Den ESP32 zunächst ohne Display per USB starten und flashen.
3. Spannung direkt am Displaymodul messen und auf Erwärmung oder Einbrüche beim WLAN-Betrieb achten.
4. Erst danach die Demo-Firmware mit dem Display testen. Bei Boot- oder USB-Problemen die Verdrahtung entfernen und besonders GPIO2/8/9/18/19 kontrollieren.

## BOOT-Taste

Im normalen Betrieb öffnet ein mindestens drei Sekunden langer Druck auf BOOT das Einrichtungs-WLAN. Die voreingestellte Taste liegt auf GPIO9 und ist nicht Teil des Display-Busses. BOOT nicht beim Einschalten gedrückt halten, wenn der USB-Downloadmodus nicht beabsichtigt ist. Die tatsächliche Tastenbelegung des eigenen SuperMini-Klons prüfen.
