# Lokalen Dienst mit Docker starten

Voraussetzungen: Docker Engine mit Compose-Plugin und eine entpackte Kopie dieses Projektordners auf dem Server. Der Dienst stellt das Dashboard unter `/` und die Geräteantwort unter `/v1/quota` bereit.

## Projekt herunterladen

```sh
git clone https://github.com/sandavdesigns/codex-quota-gauge-public.git
cd codex-quota-gauge-public
```

Alternativ das Repository als ZIP herunterladen und entpacken.

## 1. Mit Demo-Daten starten

```sh
cp .env.example .env
```

In `.env` drei Werte prüfen:

```dotenv
# LAN-Adresse des Docker-Hosts eintragen
LAN_IP=SERVER-IP
HTTP_PORT=8787
SOURCE=demo
```

`SERVER-IP` durch die tatsächliche private Adresse des Servers ersetzen. Die Voreinstellung `127.0.0.1` ist nur auf dem Server selbst erreichbar. Anschließend:

```sh
docker compose up -d --build
curl http://SERVER-IP:8787/v1/quota
```

Im Browser öffnet `http://SERVER-IP:8787/` das Dashboard. Mit `SOURCE=demo` sind alle Werte fiktiv und als Demo markiert. Für das ESP32 lautet die Adresse `http://SERVER-IP:8787/v1/quota`; im Einrichtungsportal werden jedoch nur Server-IP und Port eingegeben.

Ein anderer Host-Port ist möglich, etwa `HTTP_PORT=8088`. Nach `docker compose up -d` müssen Browser und ESP32 denselben Port verwenden. Der Dienst läuft im Container weiterhin auf Port 8787.

## 2. Codex-Quelle aktivieren

Die Codex-CLI wird im Container verwaltet. Zuerst interaktiv anmelden:

```sh
docker compose exec gauge codex -c 'cli_auth_credentials_store="file"' login --device-auth
docker compose exec gauge codex -c 'cli_auth_credentials_store="file"' login status
```

Den angezeigten Link und Einmalcode ausschließlich selbst im Browser verwenden. Danach in `.env` setzen:

```dotenv
SOURCE=codex
```

und den Dienst neu starten:

```sh
docker compose up -d
```

Bei erfolgreicher Quelle meldet `/v1/quota` `source: "codex"`. Fehlt Anmeldung oder Quelle, liefert der Dienst keinen Ersatz durch Demo-Daten. Das Docker-Volume `codex-auth` enthält Anmeldedaten: wie ein Passwort schützen und nicht mit `docker compose down -v` entfernen, wenn die Anmeldung erhalten bleiben soll.

## Sicherheit und Netzwerk

Der Standardendpunkt ist für ein privates LAN gedacht und hat keine Geräteaustausch-Anmeldung. Keine öffentliche Freigabe, keine Router-Portweiterleitung und keine Zugangsdaten in `.env`, Compose-Dateien oder Firmware. Ein getrenntes IoT-Netz benötigt eine Firewall-Regel vom ESP32 zum Dienst.

Das Dashboard ist nur eine Anzeige. Es nimmt keine Tokens entgegen und ersetzt weder Docker-Konsole noch Codex-Anmeldung.

## Aktualisieren

Eine neue ZIP-Version in einen frischen Projektordner entpacken und deren `compose.yaml` verwenden. Das Volume `codex-auth` enthält die Anmeldung; vor einem Wechsel sichern oder den bisherigen Compose-/Projektnamen beibehalten, wenn es erhalten bleiben soll. Für Updates derselben Installation `git pull` und danach `docker compose up -d --build` verwenden. Den Projektordner bzw. Compose-Projektnamen beibehalten, damit das Auth-Volume zugeordnet bleibt.

## Portainer: direkt als Git-Stack installieren

1. **Stacks → Add stack → Repository**, einen eigenen Stacknamen wählen.
2. Repository-URL: `https://github.com/sandavdesigns/codex-quota-gauge-public.git`.
3. Repository reference: `refs/heads/main`; Compose path: `compose.yaml`.
4. Repository authentication deaktiviert lassen: das Repository ist öffentlich.
5. Unter Environment variables `LAN_IP` auf die private Server-IP, `HTTP_PORT` auf den gewünschten freien Port (z. B. `8787`) und `SOURCE` zunächst auf `demo` setzen.
6. **Deploy the stack**. Der Build verwendet den Repository-Unterordner `service`.
7. Dashboard unter `http://SERVER-IP:PORT/` öffnen.
8. Im Container `gauge` die Konsole als Benutzer `node` öffnen, mit `codex -c 'cli_auth_credentials_store="file"' login --device-auth` anmelden und den Anmeldestatus prüfen.
9. Stack-Variable `SOURCE` auf `codex` ändern und den Stack aktualisieren.

Auf einem Docker-Standalone-Endpunkt verwenden, nicht als Swarm-Stack: `build` wird bei `docker stack deploy` nicht ausgeführt. Bei Portainer-Installationen ohne Git-Build-Unterstützung die oben beschriebene Docker-Compose-Installation verwenden. Ein bloßes Einfügen der Compose-Datei in den Webeditor genügt nicht: dort fehlt der Quellordner für `build: ./service`.


## Prüfen und Fehler eingrenzen

```sh
docker compose ps
docker compose logs gauge
curl http://SERVER-IP:8787/healthz
curl http://SERVER-IP:8787/v1/quota
```

`healthz` zeigt den Dienstzustand, `/v1/quota` die Daten für das Gerät. Bei Anmeldeproblemen zuerst `codex login status` im Container prüfen. Keine Codes, Tokens oder Credential-Dateien in Logs, Tickets oder Screenshots weitergeben.
