# WLAN mit dem Handy einrichten

Die `device`-Firmware richtet WLAN und die Adresse des lokalen Dienstes über eine temporäre, passwortgeschützte Geräte-WLAN-Verbindung ein. Die `demo`-Firmware bleibt netzwerkfrei und öffnet kein Einrichtungs-WLAN.

## Erste Einrichtung

1. `device` flashen und das Gerät per USB-C einschalten. Ohne gespeicherte Konfiguration zeigt das Display `WLAN SETUP`, den Gerätenamen, das Gerätepasswort und `192.168.4.1`.
2. Am Handy das angezeigte Geräte-WLAN wählen und das Passwort vom Display eingeben. Dieses WLAN hat keinen Internetzugang; gegebenenfalls „verbunden bleiben“ wählen.
3. Öffnet sich keine Seite automatisch, im Browser `http://192.168.4.1` eingeben. Das Handy muss dabei mit dem Geräte-WLAN verbunden bleiben.
4. Ein 2,4-GHz-WLAN aus der Liste wählen oder eine versteckte SSID eintragen und das WLAN-Passwort eingeben.
5. Unter „IP-Adresse des Servers“ die private Adresse des Docker-Servers eintragen. Im Feld „Port“ den veröffentlichten Docker-Port eintragen, standardmäßig `8787`. Kein `http://` und keinen Pfad eingeben: das Gerät ergänzt `/v1/quota` selbst.
6. „Einstellungen speichern“ wählen. Die Firmware testet zunächst die WLAN-Verbindung und speichert nur nach Erfolg. Ein falsches Passwort überschreibt die bisherige Einrichtung nicht.
7. IP und Port bei geöffneter Einrichtungsseite prüfen. Mit „Fertig – Anzeige starten“ das Geräte-WLAN schließen und die Anzeige starten.

Das Einrichtungs-WLAN schließt außerdem nach fünf Minuten ohne Bedienung. Jeder Seitenaufruf, eine Suche, ein Speichern oder BOOT verlängert dieses Zeitfenster.

## Einrichtung erneut öffnen

Wenn das gespeicherte WLAN nicht erreichbar ist, öffnet das Gerät die Einrichtung nach etwa 45 Sekunden. Alternativ im normalen Betrieb BOOT mindestens drei Sekunden drücken und loslassen. Die Standardbelegung ist GPIO9; beim Einschalten kann dieselbe Taste je nach Board den Downloadmodus auswählen.

Es wird genau eine aktive WLAN-/Server-Konfiguration gespeichert. Beim Standortwechsel neu einrichten. Mehrere Geräte können unabhängig eingerichtet werden.

## Netzwerkgrenzen

Die Firmware unterstützt 2,4-GHz-WLAN mit üblichen Personal-/PSK-Zugangsdaten. WLANs mit 802.1X/WPA-Enterprise (Benutzername, Zertifikat) und vorgeschaltete Browser-Anmeldeseiten werden nicht eingerichtet.

Der ESP32 muss den Dienst selbst erreichen können. Ein VPN oder eine Freigabe nur auf dem Handy hilft nach Abschluss der Einrichtung nicht. Gastnetze können Clients voneinander oder vom Dienst trennen. In diesen Fällen kann WLAN verbunden sein, während die Anzeige `LOST` oder `NO DATA` zeigt.

Der Standardaufbau verwendet HTTP ausschließlich im privaten, vertrauenswürdigen Netz. Diese Firmware richtet keinen VPN-Tunnel ein und veröffentlicht keinen Dienst ins Internet. Keine OpenAI-Anmeldedaten auf das Gerät kopieren.

Für Zugriff außerhalb des Heimnetzes: [REMOTE.md](REMOTE.md). Dort wird die eigene Domain über HTTPS auf Port 443 eingerichtet; der interne Docker-Port wird nicht am Gerät verwendet.

## Was gespeichert wird

WLAN-Name, WLAN-Passwort, Dienst-URL und gegebenenfalls HTTPS-Server-Schlüssel liegen in ESP32-Preferences im Flash/NVS und bleiben bei Stromverlust erhalten. Die Firmware aktiviert keine Flash-Verschlüsselung. Bei physischem Zugriff auf das Gerät können diese Daten deshalb unter Umständen auslesbar sein. Die Speicherung verwendet zwei Slots; auch vorherige Zugangsdaten können im inaktiven Slot verbleiben.

Vor Weitergabe des Geräts Flash einschließlich NVS löschen und anschließend neu flashen:

```sh
cd firmware
pio run -e device -t erase --upload-port <SERIELLER-PORT>
```

Das Passwort des temporären Geräte-WLANs wird einmal pro Gerät erzeugt und gespeichert. Die Seite zeigt gespeicherte WLAN-Passwörter nicht an. Die Eingaben werden im passwortgeschützten Geräte-WLAN per HTTP übertragen; keine Zugangsdaten in Screenshots oder Supportanfragen teilen.

## Abnahme

- Erststart: Gerätename, Passwort und `192.168.4.1` werden angezeigt.
- Handy: manueller Aufruf der Seite funktioniert trotz fehlendem Internetzugang.
- Richtiges und falsches WLAN-Passwort prüfen; eine gültige alte Konfiguration darf bei einem Fehler erhalten bleiben.
- BOOT, Fünf-Minuten-Timeout und Neustart nach erfolgreichem Speichern prüfen.
- Mit erreichbarem Dienst muss die Anzeige Daten liefern; mit nicht erreichbarem Dienst muss sie einen Fehlerzustand zeigen, nicht Demo-Daten.
