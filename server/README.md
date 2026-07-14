# Tesserae on Unraid

## Install

Option A — **Compose Manager plugin** (recommended):
1. Apps → install "Compose.Manager" if not present.
2. Add a new stack named `tesserae`, paste `docker-compose.yml` from this
   directory, set the stack directory to `/mnt/user/appdata/tesserae`.
3. Compose Up.

Option B — SSH:
```sh
mkdir -p /mnt/user/appdata/tesserae
cp docker-compose.yml /mnt/user/appdata/tesserae/
cd /mnt/user/appdata/tesserae && docker compose up -d
```

Then open `http://<unraid-ip>:8765`, set the admin password, and run the
onboarding wizard. Keep it LAN-only; do not reverse-proxy it to the internet.

Privacy note: Settings → System → "Online features" controls the only
outbound calls (update checks + anonymous widget-install counts to
api.tesserae.ink). Off is fine; upgrades then happen via
`docker compose pull`.

## Compose a dashboard

Dashboards are composed in the browser (Pages). Add widgets, arrange tiles.
The preview at `http://<unraid-ip>:8765/preview/<device_id>.png` shows the
last rendered composition once a device exists and a page is assigned.

## Pair the Inkplate

1. Settings → Devices → **Pair new device** → note the 6-digit pairing code.
2. Put the code in `firmware/include/config.h` (`PAIRING_CODE`) — or use
   `tools/tesserae_dryrun.py register` first to validate server-side setup.
3. The device self-describes at registration (`kind: esp32_client`,
   600×448, gamut `inky_7colour`); Tesserae creates the instance and
   returns a bearer token. Assign a page to the new device and set the
   sleep interval under the device's config (presets from 1 min to 1 day).
