# Datenquelle und geprüfte Grenzen

Prüfstand 25.10.2026, lokale CLI: `codex-cli 0.164.0-alpha.11.1`.

Die offizielle [App-Server-Dokumentation](https://learn.chatgpt.com/docs/app-server#6-rate-limits-chatgpt) beschreibt `account/rateLimits/read` über den lokalen Codex-App-Server. `usedPercent` bezeichnet den Verbrauch im Kontingentfenster, `windowDurationMins` dessen Länge, `resetsAt` den nächsten Reset als Unix-Sekunden. Deshalb berechnet der Adapter `max(0, min(100, 100-usedPercent))`. Absolute Token-/Geldbudgets werden nicht abgeleitet. Die Map `rateLimitsByLimitId` hat Vorrang vor der älteren Einzelansicht `rateLimits`; der Dienst wählt ausschließlich `LIMIT_ID` (Standard `codex`).

**Im Projekt praktisch geprüft:** ChatGPT-Anmeldung, App-Server-Initialisierung und
Kontingentabfrage im Container bis zur HTTPS-Anzeige auf dem ESP32. Anzahl und Länge
der Fenster hängen vom Konto ab; aus diesem Test folgt keine Zusage für andere Konten.
Persönliche IDs, Kontostände, Tokens und rohe Antworten werden nicht mitgeliefert.

Der Adapter startet den CLI-Unterprozess ausschließlich für diese Abfrage und beendet ihn danach. Er startet weder Chats noch Inferenz. `/status` ist kein automatisierter Parser-Eingang. Undokumentierte interne HTTP-Endpunkte, Logdatei-Scraping und UI-Scraping werden nicht verwendet. Die lokale CLI kennzeichnet `app-server` in ihrer Hilfe als experimentell; die Dokumentation ist keine Garantie unveränderlicher Protokolle. Version pinnen, Updates zunächst testen.

[Offizielle Anmeldung und Headless-Betrieb](https://learn.chatgpt.com/docs/auth#login-on-headless-devices): Für den Server ist eine eigene ChatGPT-Anmeldung nötig. Gerätecode-Anmeldung kann durch Kontoeinstellungen oder Workspace-Regeln deaktiviert sein. Credential-Datei bzw. OS-Schlüsselbund und Token-Erneuerung verwaltet die CLI. Im Container wird ein beschreibbares, separates Auth-Volume verwendet. Eine API-Key-Anmeldung ist für dieses Projekt ungeeignet.

## Grenzen des Nachweises

- Der End-to-End-Aufbau wurde an einem ESP32-C3 geprüft; weitere Hardwarevarianten müssen separat getestet werden.
- Die gepinnte CLI-Version ist im offiziellen npm-Paket verfügbar. Linux-Container-Build und Demo-Start wurden am 29.10.2026 auf GitHub Actions erfolgreich geprüft. Eine eigene Gerätecode-Anmeldung auf dem jeweiligen Zielserver ist erforderlich.
- Weitere Konten, Tarife, organisationsabhängige Buckets und künftige Versionen können andere/keine Zeitfenster liefern. Es wird kein zweites Limit erfunden.
- Fehlende Prozentwerte sind unbekannt, nicht 0 %. Fehlende Resets erscheinen als `RESET --`. Ein Gesamtbudget ist nicht notwendig.
- Ein CLI-Fehler führt zu `stale` bei vorhandenem Cache, sonst `missing`. Ein erfolgreicher Abruf ohne das gewählte Kontingent entfernt alte Werte. Kein automatischer Rückfall von echten Daten auf Demo.
- Der Dienst fragt alle fünf Minuten ab. Die Anzeige ist eine periodische Kontingentübersicht; parallele Nutzung kann zwischen Abrufen bereits weiteren Verbrauch verursachen.
