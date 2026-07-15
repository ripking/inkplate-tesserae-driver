# Tesserae on Unraid

Deployed on RifkinHomelab (`192.168.1.89`). **Port 8766, not Tesserae's
default 8765** — 8765 is already taken by the `health-ingest` container.
The compose file binds Tesserae to 8766 inside the container and maps it
1:1, with `TESSERAE_HOST_IP` set so frame URLs advertised to devices
point at the host's LAN IP (bridge networking).

Admin UI: `http://192.168.1.89:8766`

## Install

Option A — SSH (how this instance was deployed):
```sh
mkdir -p /mnt/user/appdata/tesserae/data
scp docker-compose.yml unraid:/mnt/user/appdata/tesserae/
ssh unraid 'cd /mnt/user/appdata/tesserae && docker-compose up -d'
```

Option B — **Compose Manager plugin**:
1. Apps → install "Compose.Manager" if not present.
2. Add a new stack named `tesserae`, paste `docker-compose.yml` from this
   directory, set the stack directory to `/mnt/user/appdata/tesserae`.
3. Compose Up.

Then open `http://192.168.1.89:8766`, set the admin password, and run the
onboarding wizard. Keep it LAN-only; do not reverse-proxy it to the internet.

Privacy note: Settings → System → "Online features" controls the only
outbound calls (update checks + anonymous widget-install counts to
api.tesserae.ink). Off is fine; upgrades then happen via
`docker-compose pull && docker-compose up -d`.

## Compose a dashboard

Dashboards are composed in the browser (Pages). Add widgets, arrange tiles.
The preview at `http://192.168.1.89:8766/preview/<device_id>.png` shows the
last rendered composition once a device exists and a page is assigned.

## Pair the Inkplate

1. Settings → Devices → **Pair new device** → note the 6-digit pairing code.
2. Put the code in `firmware/include/config.h` (`PAIRING_CODE`) — or use
   `tools/tesserae_dryrun.py register` first to validate server-side setup.
3. The device self-describes at registration (`kind: esp32_client`,
   600×448, gamut `inky_7colour`); Tesserae creates the instance and
   returns a bearer token. Assign a page to the new device and set the
   sleep interval under the device's config (presets from 1 min to 1 day).
