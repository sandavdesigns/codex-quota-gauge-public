# Lokales JSON-Protokoll v1

`GET /v1/quota`, HTTP 200, UTF-8, `Cache-Control: no-store`. Ein Quellenfehler wird im JSON angezeigt, damit ein alter Stand sichtbar bleibt. `GET /healthz` liefert dagegen 503 bei `missing`/`stale`, sonst 200. Andere Pfade/Methoden liefern 404. Der Dienst bietet keine Schreibschnittstelle.

**Ausschließlich fiktives Beispiel:**

```json
{
  "schema_version": 1,
  "source": "demo",
  "state": "ok",
  "server_time": 1800000000,
  "updated_at": 1800000000,
  "age_seconds": 0,
  "stale_after_seconds": 900,
  "error": null,
  "limits": [
    {
      "id": "primary",
      "label": "5 STUNDEN",
      "remaining_percent": 72,
      "window_minutes": 300,
      "resets_at": 1800008540,
      "reset_in_seconds": 8540
    },
    {
      "id": "secondary",
      "label": "WOCHE",
      "remaining_percent": 38,
      "window_minutes": 10080,
      "resets_at": 1800238000,
      "reset_in_seconds": 238000
    }
  ]
}
```

| Feld | Bedeutung |
| --- | --- |
| `schema_version` | Ganzzahl 1; inkompatible Versionen werden abgelehnt. |
| `source` | `codex` für echten CLI-Adapter, `demo` für Testdaten. Niemals aus bloßer Verfügbarkeit ableiten. |
| `state` | `ok`, `missing` (keine verwertbaren Fenster), `stale` (Abruffehler, zu alt oder Reset vergangen). |
| `server_time` | Aktuelle Unix-Sekunden des Servers; dessen Uhr muss stimmen (NTP). |
| `updated_at` | Zeitpunkt letzter erfolgreicher Quellenabfrage, anfangs `null`. |
| `age_seconds` | Alter der letzten erfolgreichen Quellenabfrage, mit monotoner Uhr berechnet, anfangs `null`. HTTP-Abruf setzt es nicht zurück. |
| `stale_after_seconds` | Grenze für Alter; Standard 900. |
| `error` | `null` oder der bereinigte Code `source_unavailable`; keine Server-/Auth-Fehlertexte. |
| `limits` | Null bis zwei Fenster des explizit ausgewählten Buckets. Abwesende Fenster werden weggelassen. |
| `id` | Stabile ID `primary`/`secondary`, keine Aussage über Fensterdauer. |
| `label` | Kurze Bezeichnung, z. B. `WOCHE`; unbekannte Dauer wird nicht als Woche bezeichnet. |
| `remaining_percent` | Endliche Zahl 0–100; absolute Gesamtmenge unnötig. Kein `null` innerhalb eines gültigen Fensters. |
| `window_minutes` | Positive Ganzzahl oder `null`, wenn unbekannt. |
| `resets_at` | Unix-Sekunden oder `null`. |
| `reset_in_seconds` | Verbleibende Sekunden zum Antwortzeitpunkt, mindestens 0, sonst `null`. |

Die Firmware erhöht Alter und reduziert Countdown zwischen Antworten selbst. Bei Countdown 0 wartet sie auf neue Daten und zeigt einen veralteten Stand. WLAN-/HTTP-Fehler werden lokal zusätzlich angezeigt. Zusätzliche optionale Felder sind zulässig; Kernfelder bleiben verpflichtend. Bis zu 4096 Byte Antwortgröße. Der mitgelieferte Dienst sendet eine explizite `Content-Length` und keine Chunk-Übertragung. Direkt verbinden; bei einem vorgeschalteten Proxy diese Eigenschaft erhalten.

## Andere Datenquelle anschließen

`service/provider.mjs` kapselt die OpenAI-Anbindung. Ein eigener Adapter muss bereits normalisierte Limits `{id,label,remaining_percent,window_minutes,resets_at}` liefern und `cache.success(limits)` erst nach erfolgreicher tatsächlicher Abfrage aufrufen. Unbekannte Prozentwerte auslassen, Fehler werfen. Der HTTP-Teil und die Firmware bleiben unverändert. Bei manuellen Datei-Importen unbedingt den ursprünglichen Erhebungszeitpunkt berücksichtigen; bloßes erneutes Einlesen darf alte Werte nicht verjüngen. Ein solcher Datei-Importer ist hier bewusst nicht implementiert.
