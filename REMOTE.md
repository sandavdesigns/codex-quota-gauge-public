# Optional: unterwegs über eine eigene HTTPS-Domain

Die Firmware unterstützt HTTPS mit Zertifikats-/Hostnamenprüfung und einem eigenen
Geräteschlüssel. Die OpenAI-Anmeldung bleibt auf dem Server. Ein fremdes WLAN muss
2,4 GHz und normalen Internetzugang ohne vorgeschaltete Browser-Anmeldeseite bieten;
WPA-Enterprise und Captive-Portal-Anmeldung werden nicht unterstützt.

## Zwei getrennte Wege

- Intern: `http://192.168.1.50:8787/` (Dashboard), `/v1/quota` (bestehende LAN-Anzeige).
- Öffentlich: `https://gauge.example.org/v1/quota` → NPM → **`/v1/device-quota`**.
  Diese Backend-Route gibt ohne aktivierten Schlüssel 404 und ohne passenden Bearer 401.
- NPM muss alle übrigen öffentlichen Pfade blockieren. Die ungeschützte LAN-Route
  darf niemals direkt ins Internet weitergeleitet werden. Port 8787 und Portainer
  bleiben ohne Routerfreigabe.

Die LAN-Oberfläche bleibt auch nach Aktivierung des Geräteschlüssels nutzbar.
Ein Domainname oder HTTPS allein ersetzt die Authentifizierung nicht.

Der im Portal „Geräteschlüssel“ genannte Wert ist ein gemeinsamer Server-Zugriffsschlüssel: mehrere eigene Anzeigen dürfen denselben Wert verwenden. Er ist kein individuelles Gerätelogin; ein Widerruf betrifft alle damit eingerichteten Geräte.

## 1. Schlüssel im bestehenden Portainer-Container erzeugen

Stack zuerst aus dem aktualisierten Repository neu bauen/deployen. In der Konsole
des Containers `gauge` als Benutzer `node` einmal ausführen:

```sh
node -e 'const fs=require("fs"),c=require("crypto");fs.writeFileSync("/home/node/.codex/gauge-device-token",c.randomBytes(32).toString("base64url")+"\n",{mode:0o600,flag:"wx"});console.log("Geräteschlüssel gespeichert")'
```

Die Datei liegt im vorhandenen persistenten Volume; `wx` verhindert versehentliches
Überschreiben. Falls sie bereits existiert, den vorhandenen Schlüssel weiterverwenden.
In den Stack-Umgebungsvariablen hinzufügen:

```dotenv
DEVICE_TOKEN_FILE=/home/node/.codex/gauge-device-token
```

Die Variable muss unter `services.gauge.environment` durchgereicht sein. Für einen
Portainer-Git-Stack kann im Compose-Environment der Eintrag
`DEVICE_TOKEN_FILE: ${DEVICE_TOKEN_FILE:-}` ergänzt werden; die mitgelieferte
Basisdatei enthält diese Durchreichung. Stack danach neu deployen. Keine Tokens in
Umgebungsvariablen, Compose, Git oder Chat eintragen.

Nur zur direkten Eingabe ins Geräteportal den Inhalt in deiner eigenen Konsole
anzeigen; anschließend das Konsolenfenster schließen:

```sh
node -e 'console.log(require("fs").readFileSync("/home/node/.codex/gauge-device-token","utf8").trim())'
```

Die Ausgabe steht mit diesem Befehl auf einer eigenen Zeile. Nur diese 43 Zeichen kopieren; die nächste Konsolen-Eingabeaufforderung gehört nicht dazu.

Der Schlüssel ist ausschließlich für die Anzeige, kein OpenAI-Token. Wer ihn hat,
kann die Kontingentwerte lesen. Für einen Austausch eine neue Zufallsdatei anlegen,
Dienst neu starten und den neuen Schlüssel im Gerät speichern.

### Alternative: Compose-Secret als Datei auf dem Host

`compose.remote.yaml` bindet `${DEVICE_TOKEN_PATH:-./secrets/device-token.txt}` als
Secret ein. Datei außerhalb von Git erstellen; sie muss vom Container-Benutzer
UID 1000 lesbar sein. Beispiel auf einem Linux-Host als Administrator:

```sh
install -d -m 700 secrets
(umask 077; openssl rand -hex 32 > secrets/device-token.txt)
chown 1000:1000 secrets/device-token.txt
chmod 400 secrets/device-token.txt
docker compose -f compose.yaml -f compose.remote.yaml up -d --build
```

Die Datei nicht bei jedem Neustart neu erzeugen. Nur eine der beiden Varianten
verwenden; gleicher Stackname erhält das Codex-Login-Volume.

## 2. DNS und Zertifikat

`gauge.example.org` ist ein Platzhalter: überall durch deine eigene Domain ersetzen.
Ein A-/AAAA-Eintrag oder ein CNAME zu deinem DynDNS-Namen muss auf deinen Proxy zeigen.
Im NPM-Beispiel `192.168.1.50:8787` durch deinen internen Server und Port ersetzen.
Widersprüchliche DNS-Einträge entfernen.

NPM → Certificates → Add Certificate → Let's Encrypt via HTTP → `gauge.example.org`.
Port 80 muss für die HTTP-01-Prüfung am NPM ankommen. Kein Gauge-Container muss dafür
ungeschützt veröffentlicht werden. Bestehende Routerweiterleitungen des NPM nutzen.
Das Zertifikat wird durch NPM erneuert. Die Firmware vertraut ISRG Root X1;
der Proxy muss eine dazu passende vollständige Let's-Encrypt-Kette senden.

## 3. Nginx Proxy Manager einrichten

Erst aktivieren, wenn der geschützte Backend-Endpunkt verfügbar ist:

- Domain: `gauge.example.org`
- Scheme: `http`; Forward Host: `192.168.1.50`; Forward Port: `8787`
- Cache Assets aus; Websocket Support aus
- SSL: Zertifikat für gauge.example.org, **Force SSL** ein
- Advanced: gesamten Inhalt von [deploy/npm-quota-location.conf](deploy/npm-quota-location.conf)
  einsetzen. Keine weitere Custom Location anlegen, die das LAN-Dashboard freigibt.

Die Advanced-Konfiguration erlaubt exakt `/v1/quota` und den ACME-Challenge-Pfad.
Sie schreibt den erlaubten API-Abruf auf den geschützten Backend-Pfad um, verbietet
Query-Parameter und andere Methoden und überträgt Authorization zum Dienst.
Das API-Zugriffslog ist abgeschaltet; keine eigenen Logs mit Authorization ergänzen.
Bei fehlgeschlagener Nginx-Konfigurationsprüfung den Host deaktiviert lassen.

## 4. Gerät einmalig umstellen

BOOT drei Sekunden drücken, Handy mit dem Einrichtungs-WLAN verbinden und
`http://192.168.4.1` öffnen:

- Verbindung **HTTPS**
- Server **gauge.example.org**
- Port **443**
- Geräteschlüssel aus deiner lokalen Container-Konsole eintragen
- Speichern und „Fertig – Anzeige starten“

`/v1/quota` wird automatisch ergänzt. Die HTTPS-Anmeldung wird nicht auf HTTP
heruntergestuft; Redirects werden nicht verfolgt. Der Schlüssel wird nicht über
HTTP versendet oder im Portal zurückgegeben. Bei leerem Schlüssel-Eingabefeld wird
der bereits gespeicherte Schlüssel behalten. NVS enthält ihn im Gerät; beim
Weitergeben des Geräts Einstellungen löschen bzw. den Schlüssel austauschen.

Die TLS-Prüfung braucht eine gültige Uhrzeit. Das Gerät synchronisiert nach dem
WLAN-Verbindungsaufbau per SNTP (`pool.ntp.org`, `time.cloudflare.com`, UDP 123).
Ohne plausible Uhrzeit startet es keine HTTPS-Abfrage. Bei Firmennetzen mit
TLS-Inspektion oder gesperrtem SNTP ist diese Verbindung gegebenenfalls nicht nutzbar.

Danach wird beim Standortwechsel nur das WLAN geändert. Domain und Schlüssel bleiben
auch nach Neustarts gespeichert. Bestehende LAN-Einstellungen werden beim Firmware-
Update nicht automatisch geändert, solange der öffentliche Zugang noch vorbereitet wird.

## 5. Abnahme

Von einem anderen Netz, z. B. Handy-Mobilfunk, prüfen:

- `/`, `/healthz`, `/v1/device-quota` und `/app.js`: 404, kein Dashboard.
- `/v1/quota` ohne/falschen Schlüssel: 401, keine Kontingentwerte.
- `/v1/quota` mit richtigem Authorization-Header: JSON und echte Quelle `codex`.
- Gültiges Zertifikat, HTTP wird auf HTTPS umgeleitet.
- ESP32 zeigt nach Umstellung echte Werte und neue Aktualisierungen.

Secrets nicht als URL-/Query-Parameter testen. Nginx-/Docker-Konfiguration, Backend-
Authentifizierung und Firmware-Build sind automatisiert prüfbar. Erst die reale
Abfrage über Domain und Gerät bestätigt die vollständige Einrichtung.

Offizielle Grundlagen: [NPM](https://nginxproxymanager.com/guide/),
[NGINX proxy_pass](https://nginx.org/en/docs/http/ngx_http_proxy_module.html#proxy_pass).

## Keine Daten nach erfolgreichem Speichern

Öffentlich wird **443**, nicht der interne Stack-Port verwendet. Im Geräteportal
HTTPS, eigene Domain und Port 443 eintragen. Bei negativen HTTPS-Verbindungsstatus
versucht die Firmware einmal öffentliche DNS-Resolver (1.1.1.1/8.8.8.8) und stellt
danach die ursprünglichen Resolver wieder her. Das kann in restriktiven Netzen
scheitern. Zertifikatsprüfung bleibt aktiv; HTTP-401 bedeutet Schlüssel prüfen.
