# esp32flight data model

What the firmware knows about every object it shows, where each field comes
from and when it may be missing. The practical consumer interface is the
`GET /api/state` JSON (bottom of this page); the C structs live in
`main/flight_model.h`, `main/ships.h`, `main/extras.h`, `main/airspace.h`.

## 1. Aircraft (raw ADS-B, refreshed every ~8 s)

Source: airplanes.live point API, merged with adsb.lol (union, dedup by hex);
alternatively a local dump1090/readsb receiver. Up to 80 aircraft in range,
40 nearest shown.

| Field | Type | Always? | Notes |
|---|---|---|---|
| `hex` | string(6) | yes | ICAO 24-bit address, the stable aircraft id |
| `callsign` | string(8) | mostly | trimmed; empty for some GA traffic |
| `reg` | string | often | registration, e.g. SP-LVD |
| `type_icao` | string(4) | often | e.g. A20N, B738 |
| `type_desc` | string | sometimes | "AIRBUS A-320neo"; airplanes.live only |
| `lat`, `lon` | double | `has_pos` | MLAT targets may lag |
| `on_ground` | bool | yes | |
| `alt_baro_ft` | int | yes | barometric altitude |
| `gs_kts` | float | yes | ground speed |
| `track_deg` | float | yes | -1 when unknown |
| `baro_rate_fpm` | int | yes | vertical rate, +climb / -descent |
| `dist_nm`, `dir_deg` | float | yes | distance/bearing from home (server-side) |
| `category` | string(2) | often | ADS-B emitter category (A1..C3) |
| `squawk` | string(4) | often | 7500/7600/7700 trigger emergency alerts |
| `military` | bool | yes | airplanes.live dbFlags bit |

Derived on device: `flight_class` (airliner / light / helicopter / military /
other - from category + military bit), `interesting` (military, notable
heavies, watchlist match), trail (last 12 positions, kept per hex).

## 2. Route and airline (per callsign, cached)

Sources in order: adsb.lol routeset (position-validated), adsbdb.com,
hexdb.io; every candidate is checked against the aircraft's actual position,
track and climb/descent phase before display. An optional FlightAware AeroAPI
key overrides all of it with live flight-plan data and adds the ticket-style
flight number (FR4238).

`route_info_t` = airline name + ICAO code + two `airport_t`:

| Airport field | Notes |
|---|---|
| `icao`, `iata` | EPGD, GDN |
| `name`, `city` | "Gdansk Lech Walesa", "Gdańsk" |
| `country` | ISO 3166-1 alpha-2, drives the flag asset |
| `lat`, `lon` | for the route map and progress math |
| `tz_offset_s`, `tz_known` | local clock at the airport |

Derived: great-circle progress %, remaining km, ETA (from ground speed),
arrival time in the destination's local clock, approach detection against the
bundled runway database ("landing GDN in ~2 min"), spotter line (compass
direction + elevation where to look), CPA flyover prediction (when and how
close it will pass).

Extra per-aircraft enrichments: airline logo (bundled or fetched from the
esp32flight-logos repo), aircraft photo (planespotters.net via adsbdb),
registration country flag (prefix table).

## 3. Optional extra objects

**Ship** (aisstream.io websocket, own key): `mmsi`, `name`, `lat/lon`,
`sog_kt`, `cog_deg`, `dist_km`, plus from ShipStaticData when broadcast:
`dest` (voyage destination) and `stype` (AIS type code -> CARGO/TANKER/TUG/
FISHING/...). TTL 10 min.

**Radiosonde** (SondeHub, keyless): `serial`, `type` (RS41-SG, M20...),
`lat/lon`, `alt_m`, `vel_v` (+up/-down), `dist_km`. 250 km range, refresh 4 min.

**ISS** (wheretheiss.at, keyless): `lat/lon`, `alt_km`, `dist_km` of the
ground track, `elev_deg`/`az_deg` above the observer's horizon. Refresh 30 s.

**Airspace** (openAIP, own key): up to 28 outlines, each `name`,
`type` (CTR/TMA/ATZ/R/D/P/TRA/MCTR/MTMA), decimated polygon (<=48 points).
Fetched once per location.

## 4. Context data

- **Weather** (Open-Meteo): temp, wind speed/direction, condition code with
  localized description; city name from reverse geocoding.
- **METAR + TAF** (aviationweather.gov) for the nearest airport; raw string
  and an on-device decoded one-liner (wind/vis/phenomena/clouds/temp/QNH).
- **Session stats**: unique aircraft, max altitude/speed/distance with
  callsign, per-hour histogram, top airlines, 7-day daily records.
- **Logs** (SPIFFS): spotting history (epoch, hex, callsign, type, airline;
  `GET /api/log` TSV) and alert history (`GET /api/alerts`).

## 5. The JSON (`GET /api/state`)

Top level: `city`, `lat`, `lon`, `radius_km`, `metric`, `weather{}`, `net{}`
(ssid, rssi, ip, mdns, heap), `iss{}`, `sondes[]`, `ships[]`, `stats{}`
(unique_aircraft, max_alt_ft, max_gs_kt, max_dist_km, max_dist_callsign,
hours[24], top_airlines[], days[], metar, taf, uptime_min, version),
`ota_enabled`, and `flights[]`:

```
hex, callsign, reg, cc, type, airline, flight_iata?, lat, lon, alt_ft,
gs_kt, track, dist_km, squawk, interesting?, trail[[lat,lon]...],
route{ from, to, from_city, to_city, from_cc, to_cc, from_time, to_time,
       from_lat, from_lon, to_lat, to_lon, progress }
```

Fields marked `?` appear only when known. Units in the JSON are always
aviation (ft/kt); `metric` tells the client the user's display preference.
`GET /api/config` / `POST /api/config` carry every setting; see the API tab
in the web panel for the full endpoint list.
