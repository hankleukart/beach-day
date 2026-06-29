# TRMNL Beach Day Announcer

A beautiful, high-contrast, e-ink optimized layout for a TRMNL device that decides whether it's a good day to go to the beach based on real-time weather, UV, and air quality conditions.

---

## How It Works

The system is split into two layers to enforce a clean separation of concerns:
1. **The Data Layer (Hubitat):** An open-source, keyless [Open-Meteo Weather Enhanced](open-meteo-weather-enhanced.groovy) driver gathers weather, UV, and AQI metrics. A dedicated [Beach Day TRMNL Integrator](beach-day-trmnl-integrator.groovy) app handles the business logic (hourly forecast merging, active beach-hour calculations, and value rounding) and sends a single, clean JSON payload to TRMNL.
2. **The Presentation Layer (TRMNL):** The [beach_day.liquid](beach_day.liquid) template renders the layout. It uses container-query units (`cqh`/`cqw`) so the layout scales natively on both the TRMNL OG and TRMNL X screens.

---

## Beach Day Criteria

To qualify for a **Beach Day**, all of the following rules must pass:

1. **Sun:** The current sky must be *Clear* or *Mainly Clear* (WMO codes 0 or 1), OR it must be forecasted to clear to those states by **2:00 PM** or earlier.
2. **High Temp:** $\ge 75^\circ\text{F}$
3. **Rain Chance:** $< 20\%$ during active beach hours (9:00 AM – 6:00 PM).
4. **Max Wind:** $\le 15\text{ mph}$ (daily max wind speed).
5. **Air Quality:** $\text{AQI} < 100$ (US AQI).
6. **Current Time:** Current local time is between `Sunrise` and `Sunset - 60 minutes`.

---

## Visual States

The left-side hero panel displays one of 8 distinct states based on a top-down priority evaluation:

* 🌌 **Night Time:** (*"Beach is closed"*) Triggered after sunset. Automatically flips the entire dashboard (including the checklist and forecast text) to show tomorrow's forecast so you can plan ahead.
* 🏡 **Indoor Day:** (*"Air Purifier On Max"*) Triggered during the day if $\text{AQI} \ge 100$, bypassing all other weather fallbacks to warn you about hazardous air (wildfire smoke, heavy smog, etc.).
* 🏖️ **Beach Day!:** (*"Pack the car"*) Triggered if all comfort rules pass.
* 🌧️ **Rain Day:** (*"Stay dry"*) Triggered if the rain chance is $\ge 40\%$.
* 💨 **Wind Day:** (*"Hold your towel"*) Triggered if max wind is $> 15\text{ mph}$.
* 🧥 **Brrr Day:** (*"Wear a hoodie"*) Triggered if the temperature is $< 65^\circ\text{F}$.
* 🌤️ **Nice Day:** (*"But not quite beachy"*) Triggered if the temperature is between $65^\circ\text{F}$ and $74^\circ\text{F}$ and it is sunny or clearing by 2:00 PM.
* ☁️ **Grey Day:** (*"Seattle vibes"*) Triggered if it is dry and mild, but the sun is not forecasted to clear by 2:00 PM.

---

## Layout Features

* **Sun Column:** If it is currently cloudy but clearing later, the checklist row will display the exact hour the sun is expected (e.g., `"At 11 AM"` or `"At 1 PM"`). If it clears after 2:00 PM, it will display the time (e.g. `"At 3 PM"`) but show a fail cross ($\mathbf{\times}$) since it is too late to qualify as a beach day.
* **Integrated UV Index:** The UV index is appended directly to the end of the text forecast description at the top of the panel (e.g. `Mostly sunny. UV Index: 6`) to keep the checklist clean.
* **Rounded Values:** All temperatures, wind speeds, UV index, and AQI values are rounded to the nearest integer before sending to TRMNL for clean, glanceable display.

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
6. Click **Done**.

### Step 4: Update your TRMNL Markup
1. Copy the entire contents of [beach_day.liquid](beach_day.liquid).
2. Go to your **TRMNL Dashboard** > **Plugins** > select your **Beach Day** private plugin.
3. Click **Edit Markup**, select all, paste the new code, and click **Save**.
