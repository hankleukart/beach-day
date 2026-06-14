# TRMNL Beach Day Announcer Layout

A beautiful, high-contrast, e-ink optimized layout for a TRMNL device that announces whether it's a good beach day or not based on real-time weather conditions.

---

## Beach Day Criteria

The layout automatically evaluates the following five rules using Shopify's Liquid templating engine:

1. **High Temperature** &ge; `74°F`
2. **Precipitation Chance** &lt; `20.0%`
3. **Current Time** is between `Sunrise` and `Sunset - 60 minutes`
4. **Detailed Forecast Description** contains `"sunny"` (case-insensitive)
5. **Wind Speed** &le; `16.0 mph`

---

## Files

* [beach_day.liquid](beach_day.liquid) - The Liquid template with styling, layout components, and logic checking.
* [sample_data.json](sample_data.json) - Mock payloads for different test scenarios.

---

## Installation & Setup Instructions

### Step 1: Connect your Weather Data
To drive the Beach Day Announcer, you need to connect your weather data sources in your TRMNL account.

1. Connect a native weather/calendar plugin or set up a private webhook plugin to collect local forecast data.
2. In **Playlists**, make sure the source plugin is connected (you can click the eyeball icon to "hide" it if you only want to use its data without showing its default template).

### Step 2: Create a Private Plugin
1. Go to your TRMNL Account dashboard.
2. Navigate to **Plugins** > **Private Plugin** and click **Create New**.
3. Fill out the basic plugin details:
   - **Name:** Beach Day Indicator
   - **Strategy:** Select **Plugin Merge** (this allows you to reference variables from other connected plugins).
4. Click **Create Private Plugin**.

### Step 3: Copy the Markup
1. Click **Edit Markup** in your newly created private plugin settings.
2. Copy the entire contents of [beach_day.liquid](beach_day.liquid) and paste it into the markup editor.
3. Click **Save**.

### Step 4: Verify with Mock Data
To test the layout in the TRMNL live preview:
1. Open the "Merge Variables" JSON input box below the editor.
2. Copy one of the JSON states from [sample_data.json](sample_data.json) (e.g., `beach_day_yes` or `beach_day_no_due_to_wind`).
3. Paste it directly as the root JSON payload.
4. Click **Force Refresh** to preview the rendering of your Beach Day Announcer!

---

## Variable Mapping Reference

The Liquid template is designed to automatically detect and map these fields, supporting both camelCase and snake_case naming structures:

| Condition | Primary Variable | Alternative Variables | Target / Rule |
| :--- | :--- | :--- | :--- |
| **High Temperature** | `temperatureHi` | `temperature_hi`, `temperature` | &ge; 74 |
| **Precipitation Chance** | `probablityofPrecipitation` | `probability_of_precipitation`, `precipitation` | &lt; 20.0 |
| **Wind Speed** | `windspeedHi` | `wind_speed_hi`, `wind_speed` | &le; 16.0 |
| **Forecast Description** | `detailedforecastlowercase` | `detailed_forecast`, `forecast` | Contains "sunny" |
| **Current Time** | `time` | `current_time` | Sunrise &le; Time &le; Sunset-60m |
| **Sunrise Time** | `sunrise` | - | Unix Epoch or minute-of-day |
| **Sunset Time** | `sunset` | - | Unix Epoch or minute-of-day |

### Time Formats Supported
* **Unix Epoch (Seconds):** e.g., `time: 1765300000`. The template automatically detects values > 86400 and subtracts 3600 seconds (60 minutes) from Sunset.
* **Minutes of Day:** e.g., `time: 540` (9:00 AM). The template subtracts 60 minutes from Sunset.
