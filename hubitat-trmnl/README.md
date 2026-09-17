# TRMNL Beach Day Announcer

A beautiful, high-contrast, e-ink optimized layout for a TRMNL device that decides whether it's a good day to go to the beach based on real-time weather, UV, and air quality conditions.

> This is the Hubitat + TRMNL target. It needs a Hubitat hub and a TRMNL
> account. For a self-contained version with neither, see
> [reterminal-e1001/](../reterminal-e1001/). The thresholds and state table
> below are mirrored in [shared/rules.json](../shared/rules.json) — change them
> there first.

---

## How It Works

The system is split into two layers to enforce a clean separation of concerns:
1. **The Data Layer (Hubitat):** An open-source, keyless [Open-Meteo Weather Enhanced](open-meteo-weather-enhanced.groovy) driver gathers weather, UV, and AQI metrics. The driver also does all daylight-window aggregation (per-day sunrise-to-sunset max rain chance, AQI and humidity ranges, the dominant weather code, and the sunny-hour scan), since only it holds the full 48-hour hourly arrays. A dedicated [Beach Day TRMNL Integrator](beach-day-trmnl-integrator.groovy) app forwards those values, evaluates the street-parking schedule, and sends a single, clean JSON payload to TRMNL.
2. **The Presentation Layer (TRMNL):** The [beach_day.liquid](beach_day.liquid) template renders the layout. It uses container-query units (`cqh`/`cqw`) so the layout scales natively on both the TRMNL OG and TRMNL X screens.

---

## Beach Day Criteria

To qualify for a **Beach Day**, all of the following rules must pass:

All hourly aggregates below use **that day's own sunrise-to-sunset hours**.

1. **Sun:** At least **3 sunny hours** (WMO codes 0, 1, or 2) in the window, AND
   either the day's dominant condition is sunny OR the sun arrives by **2:00 PM**.
2. **High Temp:** $\ge 75^\circ\text{F}$
3. **Rain Chance:** $< 20\%$ (max hourly probability in the window).
4. **Max Wind:** $\le 15\text{ mph}$ (daily max wind speed).
5. **Air Quality:** $\text{AQI} < 100$ (max US AQI in the window).
6. **Current Time:** Current local time is between `Sunrise` and `Sunset - 60 minutes`.

---

## Visual States

The left-side hero panel displays one of 9 distinct states based on a top-down priority evaluation:

* 🌌 **Night Time:** (*"Beach is closed"*) Triggered after sunset. Automatically flips the entire dashboard (including the checklist and forecast text) to show tomorrow's forecast so you can plan ahead.
* 🏡 **Indoor Day:** (*"Air Purifier On Max"*) Triggered during the day if $\text{AQI} \ge 100$, bypassing all other weather fallbacks to warn you about hazardous air (wildfire smoke, heavy smog, etc.).
* 🏖️ **Beach Day!:** (*"Pack the car"*) Triggered if all comfort rules pass.
* 🌧️ **Rain Day:** (*"Stay dry"*) Triggered if the rain chance is $\ge 40\%$.
* 💨 **Wind Day:** (*"Hold your towel"*) Triggered if max wind is $> 15\text{ mph}$.
* 🧥 **Brrr Day:** (*"Wear a hoodie"*) Triggered if the temperature is $< 65^\circ\text{F}$.
* 🌤️ **Nice Day:** (*"But not quite beachy"*) Triggered if the temperature is between $65^\circ\text{F}$ and $74^\circ\text{F}$ and the sun condition passes.
* ☁️ **Grey Day:** (*"Seattle vibes"*) Triggered if it is dry and mild, but the sun condition fails — fewer than 3 sunny hours, or no sun until after 2:00 PM.
* 🌥️ **Just A Day:** (*"Not beachy"*) Final fallback when no other state matches.

---

## Layout Features

* **Sun Column:** If the window does not start sunny but clears later, the row shows the hour the sun is expected (e.g. `"At 11 AM"`). If it clears after 2:00 PM it still shows the time (e.g. `"At 3 PM"`) but with a fail cross ($\mathbf{\times}$), since that is too late to qualify. Sun for all but at most one hour of the window reads `"All day"`; anything less shows a count (e.g. `"5 hrs"`).
* **Integrated UV Index:** The UV index is appended directly to the end of the text forecast description at the top of the panel (e.g. `Mostly sunny. UV Index: 6`) to keep the checklist clean.
* **Rounded Values:** Temperatures, wind speeds, UV index, and AQI are rounded to the nearest integer — **once**, half-up, straight from the API value. A high of 74.45 °F reads 74 and fails the 75 °F threshold.
* **Street Parking Alerts:** Optional left/right side street-cleaning reminders. When a restriction is upcoming, a high-contrast `NO PARKING LEFT SIDE: 8-10AM` block replaces the sunset/time line at the bottom of the panel. The alert appears at sunset the evening before and clears when the restriction ends the next morning.

---

## Installation & Setup

### Step 1: Install the Driver in Hubitat
1. In your Hubitat Admin Console, go to **Drivers Code** > **New Driver**.
2. Copy the entire contents of [open-meteo-weather-enhanced.groovy](open-meteo-weather-enhanced.groovy) and paste them into the editor.
3. Click **Save**.

### Step 2: Create the Virtual Device in Hubitat
1. Go to **Devices** > **Add Device** > **Virtual**.
2. Name the device (e.g., `Open-Meteo Weather`).
3. Set the **Type** to **Open-Meteo Weather Enhanced** from the dropdown.
4. Click **Save**.
5. Configure your **Latitude** and **Longitude** in the device preferences (or leave blank to use your hub's default location) and click **Save Preferences**.

### Step 3: Install and Configure the Hubitat App
1. Go to **Apps Code** > **New App**.
2. Copy the entire contents of [beach-day-trmnl-integrator.groovy](beach-day-trmnl-integrator.groovy) and paste them into the editor.
3. Click **Save**.
4. Go to **Apps** > **Add User App** > select **Beach Day TRMNL Integrator**.
5. Configure the app:
   * **Open-Meteo Weather Enhanced Device:** Select the virtual device you created in Step 2.
   * **TRMNL Webhook URL:** Paste your TRMNL private plugin webhook URL.
   * **Street Parking Restrictions:** *(optional)* Set the week(s) of the month, day, and time window for each side of the street. Disable both toggles if you don't want parking alerts.
6. Click **Done**.

> **Upgrading from an earlier version:** re-paste **all three** files. The
> daylight-window rules moved the hourly aggregation into the driver, so the
> driver, the app and the template must be updated together — an old driver with
> a new app will send zeros for the sun scan.

### Step 4: Update your TRMNL Markup
1. Copy the entire contents of [beach_day.liquid](beach_day.liquid).
2. Go to your **TRMNL Dashboard** > **Plugins** > select your **Beach Day** private plugin.
3. Click **Edit Markup**, select all, paste the new code, and click **Save**.
