/**
 *  Open-Meteo Weather Enhanced
 *  Author:  Mads Kristensen
 *  Version: 0.1.5 — 2026-05-25 — Add tomorrow's forecast attributes (high/low/condition/rain probability)
 *  License: MIT
 *
 *  Free weather driver backed by https://open-meteo.com — no API key required.
 *  Defaults to the hub's location and temperature scale, both overridable.
 *  Designed as a drop-in weather source for the Climate Advisor app: emits a
 *  human-readable WMO "weather" attribute that Climate Advisor's keyword
 *  matcher can read directly, plus next-hour and next-6h precipitation
 *  probability helpers.
 *
 *  Source: https://github.com/madskristensen/hubitat-drivers
 *
 *  Changelog:
 *    0.1.5 — 2026-05-25 — Add tomorrow's forecast attributes: temperatureMaxTomorrow, temperatureMinTomorrow, weatherTomorrow, precipitationProbabilityTomorrow.
 *    0.1.4 — 2026-05-25 — Fix: precipitationNextHour / precipitationProbabilityNextHour now cover the next 60 minutes
 *                         (previously only sampled the current hour bucket).
 *                       Fix: refresh runs immediately after install and on hub reboot (was waiting up to one poll interval).
 *                       Fix: debug auto-off timer is now scheduled from initialize() so it works after install too.
 *                       Cleanup: simplified safeResponseJson, removed duplicate unschedule, redundant `command "refresh"`,
 *                       and dead parse() method.
 *    0.1.3 — 2026-05-25 — Renamed to "Open-Meteo Weather Enhanced" to avoid name clash with the built-in Hubitat Open-Meteo driver
 *    0.1.2 — 2026-05-25 — Add today's high/low temperature attributes (temperatureMax, temperatureMin)
 *    0.1.1 — 2026-05-25 — Fix: fetch 2 forecast days so next-6h precip probability stays accurate late in the day
 *    0.1.0 — 2026-05-25 — Initial release
 */

import groovy.transform.Field
import groovy.json.JsonOutput

@Field static final String DRIVER_VERSION = "0.1.5"
@Field static final String FORECAST_URL = "https://api.open-meteo.com/v1/forecast"

// WMO weather interpretation codes — standard Open-Meteo descriptions
// https://open-meteo.com/en/docs (see "WMO Weather interpretation codes")
@Field static final Map<Integer, String> WMO_CODES = [
	0 : "Clear sky",
	1 : "Mainly clear",
	2 : "Partly cloudy",
	3 : "Overcast",
	45: "Fog",
	48: "Depositing rime fog",
	51: "Light drizzle",
	53: "Moderate drizzle",
	55: "Dense drizzle",
	56: "Light freezing drizzle",
	57: "Dense freezing drizzle",
	61: "Slight rain",
	63: "Moderate rain",
	65: "Heavy rain",
	66: "Light freezing rain",
	67: "Heavy freezing rain",
	71: "Slight snow fall",
	73: "Moderate snow fall",
	75: "Heavy snow fall",
	77: "Snow grains",
	80: "Slight rain showers",
	81: "Moderate rain showers",
	82: "Violent rain showers",
	85: "Slight snow showers",
	86: "Heavy snow showers",
	95: "Thunderstorm",
	96: "Thunderstorm with slight hail",
	99: "Thunderstorm with heavy hail"
]

metadata {
	definition (
		name: "Open-Meteo Weather Enhanced",
		namespace: "mads",
		author: "Mads Kristensen",
		importUrl: "https://raw.githubusercontent.com/madskristensen/hubitat-drivers/main/drivers/open-meteo/open-meteo-weather-enhanced.groovy"
	)
	{
		capability "Sensor"
		capability "Polling"
		capability "Refresh"
		capability "Initialize"
		capability "TemperatureMeasurement"
		capability "RelativeHumidityMeasurement"
		capability "PressureMeasurement"
		capability "UltravioletIndex"

		// Current conditions
		attribute "weather", "string"               // Human-readable WMO description (Climate Advisor reads this)
		attribute "weatherCode", "number"           // Raw WMO code
		attribute "apparentTemperature", "number"   // "Feels like"
		attribute "cloudCover", "number"            // %
		attribute "windSpeed", "number"
		attribute "windDirection", "number"         // degrees
		attribute "windGust", "number"
		attribute "precipitationRate", "number"     // current hour precipitation
		attribute "isDay", "string"                 // "day" / "night"
		attribute "sunrise", "string"               // ISO local time
		attribute "sunset", "string"                // ISO local time
		attribute "temperatureMax", "number"        // today's forecast high
		attribute "temperatureMin", "number"        // today's forecast low
		attribute "temperatureMaxTomorrow", "number"        // tomorrow's forecast high
		attribute "temperatureMinTomorrow", "number"        // tomorrow's forecast low
		attribute "weatherTomorrow", "string"               // tomorrow's WMO description
		attribute "precipitationProbabilityTomorrow", "number" // tomorrow's daily max %
		attribute "windSpeedMax", "number"          // today's max wind speed
		attribute "airQualityIndex", "number"       // US AQI
		attribute "airQualityIndexTomorrow", "number" // tomorrow's US AQI daily max
		attribute "ultravioletIndexTomorrow", "number" // tomorrow's UV index daily max

		// Precipitation helpers (for Climate Advisor)
		attribute "precipitationNextHour", "number"            // sum over next 60 min
		attribute "precipitationProbabilityNextHour", "number" // max % next 60 min
		attribute "precipitationProbabilityNext6h", "number"   // max % next 6 hours

		// Forecast blob (for future dashboard tiles)
		attribute "hourlyForecast", "string"        // JSON array, today's remaining hours

		// Status
		attribute "lastUpdated", "string"
		attribute "status", "string"
	}

	preferences {
		input "latitude", "decimal",
			title: "Latitude",
			description: "Leave blank to use hub location (${location?.latitude})",
			required: false
		input "longitude", "decimal",
			title: "Longitude",
			description: "Leave blank to use hub location (${location?.longitude})",
			required: false
		input "units", "enum",
			title: "Units",
			required: true,
			defaultValue: "auto",
			options: [
				["auto"    : "Auto (from hub: ${location?.temperatureScale ?: 'F'})"],
				["imperial": "Imperial (°F, mph, inch)"],
				["metric"  : "Metric (°C, km/h, mm)"]
			]
		input "pollInterval", "enum",
			title: "Poll interval",
			required: true,
			defaultValue: "30",
			description: "Open-Meteo updates hourly; 30 min is the sweet spot.",
			options: [["15": "15 min"], ["30": "30 min"], ["60": "1 hr"]]
		input "logEnable", "bool",
			title: "Enable debug logging (auto-off after 30 minutes)",
			defaultValue: false
		input "txtEnable", "bool",
			title: "Enable descriptive (info) logging",
			defaultValue: true
	}
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

def installed() {
	log.info "Open-Meteo Weather Enhanced ${DRIVER_VERSION} installed"
	initialize()
}

def updated() {
	log.info "Open-Meteo Weather Enhanced ${DRIVER_VERSION} updated"
	initialize()
}

def uninstalled() {
	unschedule()
}

def initialize() {
	unschedule()
	if (logEnable) runIn(1800, "logsOff")
	Integer interval = (pollInterval ?: "30").toInteger()
	switch (interval) {
		case 15: runEvery15Minutes("refresh"); break
		case 60: runEvery1Hour("refresh"); break
		default: runEvery30Minutes("refresh")
	}
	if (txtEnable) log.info "Open-Meteo: scheduled refresh every ${interval} minutes"
	refresh()
}

def poll() {
	refresh()
}

def refresh() {
	BigDecimal lat = (settings.latitude  != null) ? (settings.latitude  as BigDecimal) : (location?.latitude  as BigDecimal)
	BigDecimal lon = (settings.longitude != null) ? (settings.longitude as BigDecimal) : (location?.longitude as BigDecimal)

	if (lat == null || lon == null) {
		log.warn "Open-Meteo: latitude/longitude not configured and hub location is missing — cannot poll"
		setStatus("error: missing latitude/longitude")
		return
	}

	Map u = resolvedUnits()
	Map query = [
		latitude          : lat,
		longitude         : lon,
		timezone          : "auto",
		temperature_unit  : u.temperature_unit,
		wind_speed_unit   : u.wind_speed_unit,
		precipitation_unit: u.precipitation_unit,
		current           : "temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,rain,showers,snowfall,weather_code,cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m",
		hourly            : "temperature_2m,relative_humidity_2m,precipitation_probability,precipitation,weather_code,wind_speed_10m,uv_index",
		daily             : "sunrise,sunset,uv_index_max,temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max,wind_speed_10m_max",
		forecast_days     : 2
	]

	Map params = [
		uri    : FORECAST_URL,
		query  : query,
		timeout: 30,
		headers: ["User-Agent": "Hubitat-OpenMeteoDriver/${DRIVER_VERSION} (https://github.com/madskristensen/hubitat-drivers)"]
	]

	if (logEnable) log.debug "Open-Meteo: GET ${FORECAST_URL} ${query}"

	try {
		asynchttpGet("httpResponse", params)
	} catch (Exception e) {
		log.error "Open-Meteo: request failed: ${e.message}"
		setStatus("error: ${e.message}")
	}

	// Fetch AQI from the Open-Meteo Air Quality API
	Map aqiParams = [
		uri    : "https://air-quality-api.open-meteo.com/v1/air-quality",
		query  : [
			latitude : lat,
			longitude: lon,
			current  : "us_aqi",
			hourly   : "us_aqi",
			timezone : "auto"
		],
		timeout: 30,
		headers: ["User-Agent": "Hubitat-OpenMeteoDriver/${DRIVER_VERSION}"]
	]

	if (logEnable) log.debug "Open-Meteo AQI: GET ${aqiParams.uri} ${aqiParams.query}"

	try {
		asynchttpGet("aqiResponse", aqiParams)
	} catch (Exception e) {
		log.error "Open-Meteo: AQI request failed: ${e.message}"
	}
}

// ── HTTP response handler ────────────────────────────────────────────────────

void httpResponse(hubitat.scheduling.AsyncResponse response, Map data = null) {
	if (response == null) {
		log.warn "Open-Meteo: null response"
		setStatus("error: null response")
		return
	}
	if (response.hasError() || response.getStatus() != 200) {
		String msg = response.getErrorMessage() ?: "HTTP ${response.getStatus()}"
		log.warn "Open-Meteo: ${msg}"
		setStatus("error: ${msg}")
		return
	}

	Map json = safeResponseJson(response)
	if (!json) {
		setStatus("error: invalid JSON")
		return
	}

	Map u = resolvedUnits()
	String tUnit = u.temperature_unit == "fahrenheit" ? "°F" : "°C"
	String wUnit = u.wind_speed_unit == "mph" ? "mph" : "km/h"
	String pUnit = u.precipitation_unit == "inch" ? "in" : "mm"

	parseCurrent(json, tUnit, wUnit, pUnit)
	parseHourly(json, tUnit, pUnit)
	parseDaily(json, tUnit)

	String stamp = new Date().format("yyyy-MM-dd'T'HH:mm:ssXXX")
	sendEvent(name: "lastUpdated", value: stamp, descriptionText: "${device.displayName} last updated")
	setStatus("ok")
}

// ── Parsers ──────────────────────────────────────────────────────────────────

private void parseCurrent(Map json, String tUnit, String wUnit, String pUnit) {
	Map cur = (json.current instanceof Map) ? (Map) json.current : null
	if (!cur) {
		log.warn "Open-Meteo: response is missing 'current' block"
		return
	}

	Integer code = toInt(cur.weather_code)
	String condition = wmoDescription(code)

	emitIfChanged("weather",             condition,                                "${device.displayName} weather is ${condition}")
	emitIfChanged("weatherCode",         code,                                     "${device.displayName} WMO code is ${code}")
	emitIfChanged("temperature",         roundN(cur.temperature_2m, 1),            "${device.displayName} temperature is ${cur.temperature_2m}${tUnit}", tUnit)
	emitIfChanged("apparentTemperature", roundN(cur.apparent_temperature, 1),      "${device.displayName} apparent temperature is ${cur.apparent_temperature}${tUnit}", tUnit)
	emitIfChanged("humidity",            toInt(cur.relative_humidity_2m),          "${device.displayName} humidity is ${cur.relative_humidity_2m}%", "%")
	emitIfChanged("pressure",            roundN(cur.pressure_msl, 1),              "${device.displayName} pressure is ${cur.pressure_msl} hPa", "hPa")
	emitIfChanged("cloudCover",          toInt(cur.cloud_cover),                   "${device.displayName} cloud cover is ${cur.cloud_cover}%", "%")
	emitIfChanged("windSpeed",           roundN(cur.wind_speed_10m, 1),            "${device.displayName} wind speed is ${cur.wind_speed_10m} ${wUnit}", wUnit)
	emitIfChanged("windDirection",       toInt(cur.wind_direction_10m),            "${device.displayName} wind direction is ${cur.wind_direction_10m}°", "°")
	emitIfChanged("windGust",            roundN(cur.wind_gusts_10m, 1),            "${device.displayName} wind gusts ${cur.wind_gusts_10m} ${wUnit}", wUnit)
	emitIfChanged("precipitationRate",   roundN(cur.precipitation, 3),             "${device.displayName} precipitation rate ${cur.precipitation} ${pUnit}", pUnit)

	String dayNight = (toInt(cur.is_day) == 1) ? "day" : "night"
	emitIfChanged("isDay", dayNight, "${device.displayName} is ${dayNight}")
}

private void parseHourly(Map json, String tUnit, String pUnit) {
	Map hourly = (json.hourly instanceof Map) ? (Map) json.hourly : null
	Map cur    = (json.current instanceof Map) ? (Map) json.current : null
	if (!hourly || !cur) return

	List<String> times       = (hourly.time instanceof List)                      ? (List<String>) hourly.time                      : []
	List temps               = (hourly.temperature_2m instanceof List)            ? (List)         hourly.temperature_2m            : []
	List hums                = (hourly.relative_humidity_2m instanceof List)      ? (List)         hourly.relative_humidity_2m      : []
	List precipProbs         = (hourly.precipitation_probability instanceof List) ? (List)         hourly.precipitation_probability : []
	List precips             = (hourly.precipitation instanceof List)             ? (List)         hourly.precipitation             : []
	List codes               = (hourly.weather_code instanceof List)              ? (List)         hourly.weather_code              : []
	List winds               = (hourly.wind_speed_10m instanceof List)            ? (List)         hourly.wind_speed_10m            : []
	List uvs                 = (hourly.uv_index instanceof List)                  ? (List)         hourly.uv_index                  : []

	if (times.isEmpty()) return

	// current.time looks like "2026-05-25T12:30"; hourly.time entries are top-of-hour ("2026-05-25T12:00")
	String curTime = cur.time as String
	String curHourKey = (curTime?.length() >= 13) ? (curTime.substring(0, 13) + ":00") : null
	int startIdx = (curHourKey != null) ? times.indexOf(curHourKey) : -1
	if (startIdx < 0) {
		// Fallback: find first entry >= current.time lexicographically (ISO times sort correctly)
		startIdx = times.findIndexOf { (it as String) >= curTime }
		if (startIdx < 0) startIdx = 0
	}

	int endIdx = Math.min(times.size(), startIdx + 24)
	List<Map> blob = []
	BigDecimal precipNext1h     = 0g
	Integer    probNext1hMax    = null
	Integer    probNext6hMax    = null
	BigDecimal currentHourUv    = null

	for (int i = startIdx; i < endIdx; i++) {
		int rel = i - startIdx
		Integer hcode = toInt(codes.size() > i ? codes[i] : null)
		Map entry = [
			time      : times[i],
			temp      : roundN(temps.size() > i ? temps[i] : null, 1),
			precip    : roundN(precips.size() > i ? precips[i] : null, 3),
			precipProb: toInt(precipProbs.size() > i ? precipProbs[i] : null),
			code      : hcode,
			condition : wmoDescription(hcode),
			wind      : roundN(winds.size() > i ? winds[i] : null, 1),
			uv        : roundN(uvs.size() > i ? uvs[i] : null, 2)
		]
		blob << entry

		if (rel == 0) currentHourUv = entry.uv as BigDecimal
		// "Next hour" = remainder of current hour bucket + next hour bucket, so we cover a full 60 min window from now.
		if (rel < 2) {
			if (entry.precip != null) precipNext1h += (entry.precip as BigDecimal)
			if (entry.precipProb != null) probNext1hMax = Math.max(probNext1hMax != null ? probNext1hMax : 0, entry.precipProb as Integer)
		}
		if (rel < 6) {
			if (entry.precipProb != null) probNext6hMax = Math.max(probNext6hMax != null ? probNext6hMax : 0, entry.precipProb as Integer)
		}
	}

	emitIfChanged("hourlyForecast", JsonOutput.toJson(blob), "${device.displayName} hourly forecast updated (${blob.size()} hours)")
	emitIfChanged("precipitationNextHour",            precipNext1h.setScale(3, java.math.RoundingMode.HALF_UP), "${device.displayName} precipitation next hour ${precipNext1h} ${pUnit}", pUnit)
	emitIfChanged("precipitationProbabilityNextHour", probNext1hMax, "${device.displayName} precipitation probability next hour ${probNext1hMax}%", "%")
	emitIfChanged("precipitationProbabilityNext6h",   probNext6hMax, "${device.displayName} precipitation probability next 6h ${probNext6hMax}%", "%")


}

private void parseDaily(Map json, String tUnit) {
	Map daily = (json.daily instanceof Map) ? (Map) json.daily : null
	if (!daily) return

	List sunrises = (daily.sunrise instanceof List) ? (List) daily.sunrise : []
	List sunsets  = (daily.sunset  instanceof List) ? (List) daily.sunset  : []
	List uvMaxes  = (daily.uv_index_max instanceof List) ? (List) daily.uv_index_max : []
	List tMaxes   = (daily.temperature_2m_max instanceof List) ? (List) daily.temperature_2m_max : []
	List tMins    = (daily.temperature_2m_min instanceof List) ? (List) daily.temperature_2m_min : []
	List codes    = (daily.weather_code instanceof List) ? (List) daily.weather_code : []
	List rainPMax = (daily.precipitation_probability_max instanceof List) ? (List) daily.precipitation_probability_max : []

	if (sunrises) emitIfChanged("sunrise", sunrises[0] as String, "${device.displayName} sunrise ${sunrises[0]}")
	if (sunsets)  emitIfChanged("sunset",  sunsets[0]  as String, "${device.displayName} sunset ${sunsets[0]}")

	if (tMaxes) {
		BigDecimal tMax = roundN(tMaxes[0], 1)
		if (tMax != null) emitIfChanged("temperatureMax", tMax, "${device.displayName} today's high is ${tMax}${tUnit}", tUnit)
	}
	if (tMins) {
		BigDecimal tMin = roundN(tMins[0], 1)
		if (tMin != null) emitIfChanged("temperatureMin", tMin, "${device.displayName} today's low is ${tMin}${tUnit}", tUnit)
	}

	if (tMaxes.size() > 1) {
		BigDecimal tMaxT = roundN(tMaxes[1], 1)
		if (tMaxT != null) emitIfChanged("temperatureMaxTomorrow", tMaxT, "${device.displayName} tomorrow's high is ${tMaxT}${tUnit}", tUnit)
	}
	if (tMins.size() > 1) {
		BigDecimal tMinT = roundN(tMins[1], 1)
		if (tMinT != null) emitIfChanged("temperatureMinTomorrow", tMinT, "${device.displayName} tomorrow's low is ${tMinT}${tUnit}", tUnit)
	}
	if (codes.size() > 1) {
		Integer codeT = toInt(codes[1])
		String condT = wmoDescription(codeT)
		if (condT) emitIfChanged("weatherTomorrow", condT, "${device.displayName} tomorrow's weather is ${condT}")
	}
	if (rainPMax.size() > 1) {
		Integer probT = toInt(rainPMax[1])
		if (probT != null) emitIfChanged("precipitationProbabilityTomorrow", probT, "${device.displayName} tomorrow's max precipitation probability ${probT}%", "%")
	}

	List windMaxes = (daily.wind_speed_10m_max instanceof List) ? (List) daily.wind_speed_10m_max : []
	Map u = resolvedUnits()
	String wUnit = u.wind_speed_unit == "mph" ? "mph" : "km/h"
	if (windMaxes) {
		BigDecimal wMax = roundN(windMaxes[0], 1)
		if (wMax != null) emitIfChanged("windSpeedMax", wMax, "${device.displayName} today's max wind speed is ${wMax}${wUnit}", wUnit)
	}
	if (windMaxes.size() > 1) {
		BigDecimal wMaxT = roundN(windMaxes[1], 1)
		if (wMaxT != null) emitIfChanged("windSpeedMaxTomorrow", wMaxT, "${device.displayName} tomorrow's max wind speed is ${wMaxT}${wUnit}", wUnit)
	}

	if (uvMaxes) {
		BigDecimal uvMax = roundN(uvMaxes[0], 2)
		if (uvMax != null) {
			emitIfChanged("ultravioletIndex", uvMax, "${device.displayName} UV index (daily max) is ${uvMax}")
		}
	}
	if (uvMaxes.size() > 1) {
		BigDecimal uvMaxT = roundN(uvMaxes[1], 2)
		if (uvMaxT != null) {
			emitIfChanged("ultravioletIndexTomorrow", uvMaxT, "${device.displayName} UV index tomorrow (daily max) is ${uvMaxT}")
		}
	}
}

void aqiResponse(hubitat.scheduling.AsyncResponse response, Map data = null) {
	if (response == null) {
		log.warn "Open-Meteo: null AQI response"
		return
	}
	if (response.hasError() || response.getStatus() != 200) {
		String msg = response.getErrorMessage() ?: "HTTP ${response.getStatus()}"
		log.warn "Open-Meteo AQI: ${msg}"
		return
	}
	Map json = safeResponseJson(response)
	if (!json) return

	Map hourly = (json.hourly instanceof Map) ? (Map) json.hourly : null
	if (hourly && hourly.us_aqi instanceof List) {
		List aqis = (List) hourly.us_aqi

		// Determine daylight hours from device attributes (default to 6 AM - 8 PM if not set)
		String sunriseStr = device.currentValue("sunrise")
		String sunsetStr = device.currentValue("sunset")
		int sunriseHour = 6
		int sunsetHour = 20

		if (sunriseStr && sunriseStr.contains("T")) {
			try {
				sunriseHour = sunriseStr.split("T")[1].split(":")[0].toInteger()
			} catch (e) {
				log.warn "Failed to parse sunrise hour from ${sunriseStr}: ${e.message}"
			}
		}
		if (sunsetStr && sunsetStr.contains("T")) {
			try {
				sunsetHour = sunsetStr.split("T")[1].split(":")[0].toInteger()
			} catch (e) {
				log.warn "Failed to parse sunset hour from ${sunsetStr}: ${e.message}"
			}
		}
		
		// Today's max (indices 0..23, filtered by daylight hours)
		List todayDaylightAqis = []
		for (int i = 0; i < 24; i++) {
			if (i >= aqis.size()) break
			if (i >= sunriseHour && i <= sunsetHour) {
				todayDaylightAqis.add(aqis[i])
			}
		}
		Integer maxAqiToday = todayDaylightAqis.findAll { it != null }.max() as Integer

		// Tomorrow's max (indices 24..47, filtered by daylight hours)
		Integer maxAqiTomorrow = null
		if (aqis.size() >= 48) {
			List tomorrowDaylightAqis = []
			for (int i = 24; i < 48; i++) {
				if (i >= aqis.size()) break
				int hourOfTomorrow = i - 24
				if (hourOfTomorrow >= sunriseHour && hourOfTomorrow <= sunsetHour) {
					tomorrowDaylightAqis.add(aqis[i])
				}
			}
			maxAqiTomorrow = tomorrowDaylightAqis.findAll { it != null }.max() as Integer
		}

		if (maxAqiToday != null) {
			emitIfChanged("airQualityIndex", maxAqiToday, "${device.displayName} AQI (daily max) is ${maxAqiToday}")
		}
		if (maxAqiTomorrow != null) {
			emitIfChanged("airQualityIndexTomorrow", maxAqiTomorrow, "${device.displayName} AQI tomorrow (daily max) is ${maxAqiTomorrow}")
		}
	} else {
		// Fallback to current if hourly is not present
		Map cur = (json.current instanceof Map) ? (Map) json.current : null
		if (cur && cur.us_aqi != null) {
			Integer aqi = toInt(cur.us_aqi)
			emitIfChanged("airQualityIndex", aqi, "${device.displayName} AQI is ${aqi}")
		}
	}
}

// ── Helpers ──────────────────────────────────────────────────────────────────

private Map resolvedUnits() {
	String mode = (settings.units ?: "auto") as String
	if (mode == "auto") {
		mode = (location?.temperatureScale == "C") ? "metric" : "imperial"
	}
	if (mode == "metric") {
		return [temperature_unit: "celsius", wind_speed_unit: "kmh", precipitation_unit: "mm"]
	}
	return [temperature_unit: "fahrenheit", wind_speed_unit: "mph", precipitation_unit: "inch"]
}

private String wmoDescription(Integer code) {
	if (code == null) return "Unknown"
	return WMO_CODES.containsKey(code) ? WMO_CODES[code] : "Unknown (code ${code})"
}

private Integer toInt(value) {
	if (value == null) return null
	if (value instanceof Number) return ((Number) value).intValue()
	String s = value.toString()
	return s.isNumber() ? s.toBigDecimal().intValue() : null
}

private BigDecimal roundN(value, int scale) {
	if (value == null) return null
	BigDecimal bd
	if (value instanceof BigDecimal) {
		bd = (BigDecimal) value
	} else if (value instanceof Number) {
		bd = BigDecimal.valueOf(((Number) value).doubleValue())
	} else {
		String s = value.toString()
		if (!s.isNumber()) return null
		bd = new BigDecimal(s)
	}
	return bd.setScale(scale, java.math.RoundingMode.HALF_UP)
}

private void emitIfChanged(String name, value, String descTxt, String unit = null) {
	if (value == null) return
	def current = device.currentValue(name)
	boolean changed
	if (current instanceof Number || value instanceof Number) {
		try { changed = (current as BigDecimal) != (value as BigDecimal) }
		catch (Exception e) { changed = current?.toString() != value?.toString() }
	} else {
		changed = current?.toString() != value?.toString()
	}
	if (!changed) return
	Map evt = [name: name, value: value, descriptionText: descTxt]
	if (unit) evt.unit = unit
	sendEvent(evt)
	if (txtEnable) log.info descTxt
}

private void setStatus(String value) {
	def current = device.currentValue("status")
	if (current?.toString() != value) {
		sendEvent(name: "status", value: value, descriptionText: "${device.displayName} status: ${value}")
	}
}

private Map safeResponseJson(hubitat.scheduling.AsyncResponse resp) {
	try {
		def parsed = resp?.getJson()
		if (parsed instanceof Map) return parsed as Map
	} catch (Exception e) {
		log.warn "Open-Meteo: invalid JSON (status ${resp?.getStatus()}): ${e.message}"
	}
	return null
}

void logsOff() {
	log.info "Open-Meteo: debug logging disabled"
	device.updateSetting("logEnable", [value: false, type: "bool"])
}
