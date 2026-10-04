# Inkplate × Tesserae

**Turn an Inkplate e-ink display into a low-power dashboard for your home:
calendar, weather, photos, whatever you like.**

You design the dashboard in a web browser using
[Tesserae](https://tesserae.ink), a free app you run on your own computer or
home server. This firmware goes on the Inkplate. Every so often the display
wakes up, downloads the latest dashboard, shows it, and goes back to sleep.
On battery it can run for weeks between charges.

> Tesserae's official firmware doesn't support Inkplate displays. That's the
> gap this project fills.

---

## What you need

- **An Inkplate 6COLOR or Inkplate 13SPECTRA** from
  [Soldered](https://soldered.com/inkplate/), plus a USB-C cable that
  carries data (some only charge).
- **A computer that's always on** to run Tesserae: a home server, NAS,
  Raspberry Pi or spare PC with [Docker](https://docs.docker.com/get-docker/)
  installed.
- **A 2.4 GHz Wi-Fi network.** The Inkplate can't connect to 5 GHz.
- **[PlatformIO](https://platformio.org/install)**, which loads the firmware
  onto the Inkplate. The easiest way to get it is the free VS Code extension.
- *Optional:* a LiPo battery, if you want the display to run unplugged.

Setup takes about 30 minutes.

---

## Setup

### Step 1: Start Tesserae

On your always-on computer, download this project and run:

```sh
cd server
docker compose up -d
```

> The included `docker-compose.yml` is set up for the author's home server.
> Before running it, open it and change `TESSERAE_HOST_IP` to your
> computer's local IP address (something like `192.168.1.50`), and the
> volume path to a folder that exists on your machine.
> [`server/README.md`](server/README.md) has more detail.

Then open `http://<your-computer's-IP>:8766` in a browser and follow the
setup wizard.

### Step 2: Design a dashboard

In Tesserae, create a **Page** and add a few widgets (a clock or weather
widget is a good first test). You can change it later.

### Step 3: Get a pairing code

In Tesserae, go to **Settings → Devices → Pair new device**. Write down
the 6-digit code; you'll need it in the next step. Each code works only
once.

### Step 4: Fill in your settings

In this project's `firmware/include` folder, make a copy of
`config.example.h` and name it `config.h`. Open it and fill in:

```c
#define WIFI_SSID         "MyHomeWiFi"
#define WIFI_PASSWORD     "my-wifi-password"
#define TESSERAE_BASE_URL "http://192.168.1.50:8766"   // your Tesserae address
#define DEVICE_ID         "inkplate_kitchen"           // any unique name, no spaces
#define DEVICE_NAME       "Kitchen Display"            // friendly name shown in Tesserae
#define PAIRING_CODE      "123456"                     // the code from Step 3
#define POWER_MODE        POWER_USB                    // see "Battery or plugged in?" below
```

`config.h` holds your Wi-Fi password, so it's excluded from git and
won't be uploaded if you push your own copy.

### Step 5: Load the firmware onto the Inkplate

Plug the Inkplate into your computer with the USB-C cable, then run the
command for your board:

```sh
cd firmware
pio run -e inkplate6color -t upload      # Inkplate 6COLOR
pio run -e inkplate13spectra -t upload   # Inkplate 13SPECTRA
```

> **Inkplate 13SPECTRA:** the dashboard is landscape (1600×1200). For a
> portrait or upside-down mount, change the device's **orientation** in
> Tesserae (Settings → Devices); no firmware change needed. A brand-new
> board can be checked first with
> `pio run -e inkplate13spectra_selftest -t upload`, which paints six
> colour stripes without needing Wi-Fi.

(In VS Code, you can instead click the PlatformIO **Upload** button.)

### Step 6: Connect the display to your dashboard

After uploading, the Inkplate connects to Wi-Fi and pairs itself. Within
a minute it should appear in Tesserae under **Settings → Devices**.

Open the device, **assign the page you made in Step 2**, and wait for the
next update. The screen takes about 20–30 seconds to redraw. That slow,
flickering redraw is normal for color e-ink.

**You're done.** 🎉 From now on, edit the dashboard in Tesserae and the
display picks up the changes on its next check-in.

---

## Battery or plugged in?

Set `POWER_MODE` in `config.h`, then upload again (Step 5).

| Mode | Best for | How it behaves |
|---|---|---|
| `POWER_USB` | First-time setup, or a display that's always plugged in | Stays awake, so you can watch what it's doing and it can update often |
| `POWER_BATTERY` | Running on a LiPo | Sleeps between updates; a battery lasts weeks |

How often the display updates is set **in Tesserae**, not in the firmware:
open the device under **Settings → Devices** and pick an interval (from
1 minute to 1 day). Updating less often makes the battery last longer.

---

## Something not working?

If something keeps failing, the display shows a message saying what's
wrong. A single hiccup won't replace your dashboard; the message appears
only after several failed tries in a row.

| The display says… | What to do |
|---|---|
| **Pairing failed** | Pairing codes work only once. Make a new one (Step 3), put it in `config.h`, and upload again. |
| **Wi-Fi unreachable** | Check the network name and password in `config.h`, and make sure it's a 2.4 GHz network. |
| **Server unreachable** | Check `TESSERAE_BASE_URL`, including the port number. Is the Tesserae computer on, and on the same network? |
| Nothing changes after pairing | Make sure you assigned a page to the device (Step 6). |
| Colors look wrong | Open an issue; this usually means the color order is off (see the [technical reference](docs/technical.md#frame-format)). |
| The upload fails | Try a different USB-C cable (it must carry data) or a different USB port. |

To see exactly what the display is doing, keep it plugged in and run
`pio device monitor` from the `firmware` folder. It prints each step as it
happens.

---

## Learn more

- [`docs/bringup.md`](docs/bringup.md): a detailed, step-by-step checklist,
  including how to test your Tesserae setup without the display.
- [`docs/technical.md`](docs/technical.md): how the firmware works, the
  frame format, file layout and tests. Read this if you want to modify the
  code.
- [`server/README.md`](server/README.md): notes on running Tesserae on
  Unraid.

## Thanks

- [Tesserae](https://github.com/dmellok/tesserae) by dmellok, which does
  the actual work of building the dashboards.
- [Soldered's Inkplate library](https://github.com/SolderedElectronics/Inkplate-Arduino-library),
  which drives the screen.

---

## License

[MIT](LICENSE)
