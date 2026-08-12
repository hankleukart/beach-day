/**
 *  Beach Day TRMNL Integrator
 *  Author: Hank Leukart
 *  License: MIT
 */

definition(
    name: "Beach Day TRMNL Integrator",
    namespace: "hankle",
    author: "Hank Leukart",
    description: "Integrates Open-Meteo Weather Enhanced device with TRMNL Beach Day layout.",
    category: "Convenience",
    iconUrl: "",
    iconX2Url: ""
)

preferences {
    page(name: "mainPage")
}

def mainPage() {
    dynamicPage(name: "mainPage", title: "", install: true, uninstall: true) {
        section("") {
            input "weatherDevice", "capability.sensor", title: "Open-Meteo Weather Enhanced Device", required: true, multiple: false
            input "locationName", "text", title: "Location Name (optional)", required: false
            input "webhookUrl", "text", title: "TRMNL Webhook URL", required: true
            input "logEnable", "bool", title: "Enable debug logging", defaultValue: true
            input name: "btnForceUpdate", type: "button", title: "Force Update Now"
        }
    }
}

def appButtonHandler(btn) {
    switch(btn) {
        case "btnForceUpdate":
            log.info "Force update clicked: refreshing weather device..."
            if (weatherDevice && weatherDevice.hasCommand("refresh")) {
                weatherDevice.refresh()
            } else {
                updateTrmnl()
            }
            break
    }
}

def installed() {
    log.info "Beach Day TRMNL Integrator installed"
    initialize()
}

def updated() {
    log.info "Beach Day TRMNL Integrator updated"
    initialize()
}

def initialize() {
    unsubscribe()
    subscribe(weatherDevice, "lastUpdated", handleWeatherUpdate)
    // Run an initial update
    runIn(2, "updateTrmnl")
}

def handleWeatherUpdate(evt) {
    if (logEnable) log.debug "Weather device updated: ${evt.value}"
    updateTrmnl()
}

def updateTrmnl() {
    if (!weatherDevice) {
        log.warn "No weather device selected"
        return
    }
    if (!webhookUrl) {
        log.warn "No TRMNL webhook URL configured"
        return
    }

    // 1. Parse Hourly Forecast & Perform Merging (Option B)
    String hourlyForecastStr = weatherDevice.currentValue("hourlyForecast")
    List<Map> parsedHourly = []
    if (hourlyForecastStr) {
        try {
            parsedHourly = new groovy.json.JsonSlurper().parseText(hourlyForecastStr) as List<Map>
        } catch (e) {
            log.error "Failed to parse hourlyForecast JSON: ${e.message}"
        }
    }

    // Simplify new periods (only keep 't' and 'c' for beach hours 9-18)
    List<Map> simplifiedNewPeriods = []
    parsedHourly.each { Map it ->
        String timeStr = it.time as String
        if (timeStr && timeStr.contains("T")) {
            def hourPart = timeStr.split("T")[1].split(":")[0].toInteger()
            if (hourPart >= 9 && hourPart <= 18) {
                simplifiedNewPeriods << [
                    t: (timeStr.length() >= 13) ? timeStr.substring(0, 13) : "",
                    c: (it.code != null) ? it.code.toInteger() : 0
                ]
            }
        }
    }

    // Merge past periods from today with the new future forecast periods
    String todayDate = location.timeZone ? new Date().format('yyyy-MM-dd', location.timeZone) : new Date().format('yyyy-MM-dd')
    String tomorrowDate = location.timeZone ? new Date().plus(1).format('yyyy-MM-dd', location.timeZone) : new Date().plus(1).format('yyyy-MM-dd')
    String currentHourStr = location.timeZone ? new Date().format('yyyy-MM-dd\'T\'HH', location.timeZone) : new Date().format('yyyy-MM-dd\'T\'HH')

    List<Map> existingPeriods = (state.hourlyPeriods as List<Map>) ?: []
    List<Map> pastPeriodsToday = existingPeriods.findAll { Map it ->
        String t = it.t as String
        return t && t.startsWith(todayDate) && t < currentHourStr
    }

    List<Map> newFuturePeriods = simplifiedNewPeriods.findAll { Map it ->
        String t = it.t as String
        return t && t >= currentHourStr
    }

    state.hourlyPeriods = (pastPeriodsToday + newFuturePeriods).take(20)

    // Calculate max precip probability during beach hours (9 AM to 6 PM) today
    int maxPrecipProb = 0
    parsedHourly.each { Map it ->
        String timeStr = it.time as String
        if (timeStr && timeStr.startsWith(todayDate)) {
            def hourPart = timeStr.split("T")[1].split(":")[0].toInteger()
            if (hourPart >= 9 && hourPart <= 18) {
                int prob = (it.precipProb != null) ? it.precipProb.toInteger() : 0
                if (prob > maxPrecipProb) {
                    maxPrecipProb = prob
                }
            }
        }
    }

    // Calculate predominant (mode) WMO code during daylight hours (9 AM to 6 PM)
    Integer codeDaylightToday = computeDaylightModeWmoCode(parsedHourly, todayDate)
    String condDaylightToday = wmoCodeToDescription(codeDaylightToday)
    Integer codeDaylightTomorrow = computeDaylightModeWmoCode(parsedHourly, tomorrowDate)
    String condDaylightTomorrow = wmoCodeToDescription(codeDaylightTomorrow)

    // 2. Parse Sunrise/Sunset
    String sunriseStr = weatherDevice.currentValue("sunrise")
    String sunsetStr = weatherDevice.currentValue("sunset")

    int sunriseVal = parseTimeToMinutes(sunriseStr)
    int sunsetVal = parseTimeToMinutes(sunsetStr)
    String sunriseFormatted = formatTime(sunriseStr)
    String sunsetFormatted = formatTime(sunsetStr)

    // 3. Gather other metrics (rounded to nearest integer for display)
    def tempHi = roundToNearest(weatherDevice.currentValue("temperatureMax"))
    def tempLow = roundToNearest(weatherDevice.currentValue("temperatureMin"))
    def windHi = roundToNearest(weatherDevice.currentValue("windSpeedMax"))
    def uv = roundToNearest(weatherDevice.currentValue("ultravioletIndex")) ?: 0
    def aqi = roundToNearest(weatherDevice.currentValue("airQualityDaylightHrsMax")) ?: 0
    def uvTomorrow = roundToNearest(weatherDevice.currentValue("ultravioletIndexTomorrow")) ?: 0
    def aqiTomorrow = roundToNearest(weatherDevice.currentValue("airQualityDaylightHrsMaxTomorrow")) ?: 0

    def tomorrowTemp = roundToNearest(weatherDevice.currentValue("temperatureMaxTomorrow"))
    def tomorrowTempLow = roundToNearest(weatherDevice.currentValue("temperatureMinTomorrow"))
    def tomorrowPrecip = roundToNearest(weatherDevice.currentValue("precipitationProbabilityTomorrow")) ?: 0
    def tomorrowWind = roundToNearest(weatherDevice.currentValue("windSpeedMaxTomorrow"))

    // Clean weather condition strings (remove trailing periods if any)
    def weatherCondToday = (weatherDevice.currentValue("weatherToday") ?: weatherDevice.currentValue("weatherNow") ?: "Unknown").toString()
    if (weatherCondToday.endsWith(".")) {
        weatherCondToday = weatherCondToday.substring(0, weatherCondToday.length() - 1)
    }
    def weatherCondTomorrow = (weatherDevice.currentValue("weatherTomorrow") ?: "Unknown").toString()
    if (weatherCondTomorrow.endsWith(".")) {
        weatherCondTomorrow = weatherCondTomorrow.substring(0, weatherCondTomorrow.length() - 1)
    }

    def humidityMin = roundToNearest(weatherDevice.currentValue("humidityMin"))
    def humidityMax = roundToNearest(weatherDevice.currentValue("humidityMax"))
    def aqiMin = roundToNearest(weatherDevice.currentValue("airQualityDaylightHrsMin"))

    def tomorrowHumidityMin = roundToNearest(weatherDevice.currentValue("humidityMinTomorrow"))
    def tomorrowHumidityMax = roundToNearest(weatherDevice.currentValue("humidityMaxTomorrow"))
    def tomorrowAqiMin = roundToNearest(weatherDevice.currentValue("airQualityDaylightHrsMinTomorrow"))

    def locName = settings.locationName ?: ""

    // 4. Construct Payload
    Map payload = [
        merge_variables: [
            location_name: locName,
            temperatureHi: tempHi,
            temperatureMin: tempLow,
            probablityofPrecipitation: maxPrecipProb, // calculated for active beach hours
            windspeedHi: windHi,
            sunrise_val: sunriseVal,
            sunset_val: sunsetVal,
            sunrise_formatted: sunriseFormatted,
            sunset_formatted: sunsetFormatted,
            tomorrowTemperature: tomorrowTemp,
            tomorrowTemperatureMin: tomorrowTempLow,
            tomorrowProbabilityOfPrecipitation: tomorrowPrecip,
            tomorrowWindSpeedHi: tomorrowWind,
            hourly: state.hourlyPeriods,
            uvIndex: uv,
            airQualityDaylightHrsMax: aqi,
            tomorrowUvIndex: uvTomorrow,
            airQualityDaylightHrsMaxTomorrow: aqiTomorrow,
            weatherCondToday: weatherCondToday,
            weatherCodeToday: weatherDevice.currentValue("weatherCodeToday") ?: weatherDevice.currentValue("weatherCodeNow") ?: 0,
            weatherCondDaylightToday: condDaylightToday ?: weatherCondToday,
            weatherCodeDaylightToday: codeDaylightToday != null ? codeDaylightToday : (weatherDevice.currentValue("weatherCodeToday") ?: weatherDevice.currentValue("weatherCodeNow") ?: 0),
            humidityMin: humidityMin,
            humidityMax: humidityMax,
            airQualityDaylightHrsMin: aqiMin,
            weatherCondTomorrow: weatherCondTomorrow,
            weatherCodeTomorrow: weatherDevice.currentValue("weatherCodeTomorrow") ?: 0,
            weatherCondDaylightTomorrow: condDaylightTomorrow ?: weatherCondTomorrow,
            weatherCodeDaylightTomorrow: codeDaylightTomorrow != null ? codeDaylightTomorrow : (weatherDevice.currentValue("weatherCodeTomorrow") ?: 0),
            tomorrowHumidityMin: tomorrowHumidityMin,
            tomorrowHumidityMax: tomorrowHumidityMax,
            airQualityDaylightHrsMinTomorrow: tomorrowAqiMin
        ]
    ]

    // 5. Send to TRMNL Webhook
    Map params = [
        uri: webhookUrl,
        body: groovy.json.JsonOutput.toJson(payload),
        requestContentType: 'application/json',
        contentType: 'application/json',
        timeout: 30
    ]

    if (logEnable) log.debug "Sending payload to TRMNL: ${params.body}"

    asynchttpPost("trmnlResponse", params)
}

void trmnlResponse(hubitat.scheduling.AsyncResponse response, Map data) {
    if (response == null) {
        log.warn "TRMNL: null response received"
        return
    }
    if (response.hasError() || response.getStatus() != 200) {
        log.warn "TRMNL Webhook failed: ${response.getErrorMessage() ?: response.getStatus()}"
    } else {
        if (logEnable) log.info "TRMNL Webhook sent successfully"
    }
}

private int parseTimeToMinutes(String isoStr) {
    if (!isoStr || !isoStr.contains("T")) return 0
    try {
        def parts = isoStr.split("T")[1].split(":")
        int h = parts[0].toInteger()
        int m = parts[1].toInteger()
        return h * 60 + m
    } catch (e) {
        log.error "Failed to parse time string '${isoStr}': ${e.message}"
        return 0
    }
}

private String formatTime(String isoStr) {
    if (!isoStr || !isoStr.contains("T")) return ""
    try {
        def parts = isoStr.split("T")[1].split(":")
        int h = parts[0].toInteger()
        int m = parts[1].toInteger()
        String ampm = h >= 12 ? "PM" : "AM"
        int displayH = h % 12
        if (displayH == 0) displayH = 12
        String displayM = m < 10 ? "0${m}" : "${m}"
        return "${displayH}:${displayM} ${ampm}"
    } catch (e) {
        log.error "Failed to format time string '${isoStr}': ${e.message}"
        return ""
    }
}

private Integer roundToNearest(value) {
    if (value == null) return null
    if (value instanceof Number) {
        return Math.round(value.doubleValue()).intValue()
    }
    try {
        String s = value.toString()
        if (s.isNumber()) {
            return Math.round(s.toDouble()).intValue()
        }
    } catch (e) {
        log.error "Failed to round value '${value}': ${e.message}"
    }
    return null
}

private Integer computeDaylightModeWmoCode(List<Map> parsedHourly, String targetDate) {
    if (!parsedHourly || !targetDate) return null
    Map<Integer, Integer> counts = [:]
    parsedHourly.each { Map it ->
        String timeStr = it.time as String
        if (timeStr && timeStr.startsWith(targetDate)) {
            def parts = timeStr.split("T")
            if (parts.size() > 1) {
                def hourPart = parts[1].split(":")[0].toInteger()
                if (hourPart >= 9 && hourPart <= 18 && it.code != null) {
                    int code = it.code.toInteger()
                    counts[code] = (counts[code] ?: 0) + 1
                }
            }
        }
    }
    if (counts.isEmpty()) return null
    return counts.entrySet().max { a, b ->
        a.value <=> b.value ?: b.key <=> a.key
    }?.key
}

private String wmoCodeToDescription(Integer code) {
    if (code == null) return null
    Map<Integer, String> wmoCodes = [
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
    return wmoCodes[code] ?: "Unknown (code ${code})"
}
