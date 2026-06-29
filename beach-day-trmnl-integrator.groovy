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

    // Simplify new periods
    List<Map> simplifiedNewPeriods = parsedHourly.collect { Map it ->
        String timeStr = it.time as String
        [
            t: (timeStr && timeStr.length() >= 13) ? timeStr.substring(0, 13) : "",
            f: it.condition ?: "Unknown"
        ]
    }

    // Merge past periods from today with the new future forecast periods
    String todayDate = location.timeZone ? new Date().format('yyyy-MM-dd', location.timeZone) : new Date().format('yyyy-MM-dd')
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

    state.hourlyPeriods = (pastPeriodsToday + newFuturePeriods).take(26)

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

    // 2. Parse Sunrise/Sunset
    String sunriseStr = weatherDevice.currentValue("sunrise")
    String sunsetStr = weatherDevice.currentValue("sunset")

    int sunriseVal = parseTimeToMinutes(sunriseStr)
    int sunsetVal = parseTimeToMinutes(sunsetStr)
    String sunriseFormatted = formatTime(sunriseStr)
    String sunsetFormatted = formatTime(sunsetStr)

    // 3. Gather other metrics (rounded to nearest integer for display)
    def tempHi = roundToNearest(weatherDevice.currentValue("temperatureMax"))
    def windHi = roundToNearest(weatherDevice.currentValue("windSpeedMax"))
    def detailed = weatherDevice.currentValue("weather")
    def uv = roundToNearest(weatherDevice.currentValue("ultravioletIndex")) ?: 0
    def aqi = roundToNearest(weatherDevice.currentValue("airQualityIndex")) ?: 0

    def tomorrowTemp = roundToNearest(weatherDevice.currentValue("temperatureMaxTomorrow"))
    def tomorrowPrecip = roundToNearest(weatherDevice.currentValue("precipitationProbabilityTomorrow")) ?: 0
    def tomorrowWind = roundToNearest(weatherDevice.currentValue("windSpeedMaxTomorrow"))
    def tomorrowDetailed = weatherDevice.currentValue("weatherTomorrow")

    // 4. Construct Payload
    Map payload = [
        merge_variables: [
            temperatureHi: tempHi,
            probablityofPrecipitation: maxPrecipProb, // calculated for active beach hours
            windspeedHi: windHi,
            detailedForecast: detailed,
            sunrise_val: sunriseVal,
            sunset_val: sunsetVal,
            sunrise_formatted: sunriseFormatted,
            sunset_formatted: sunsetFormatted,
            tomorrowTemperature: tomorrowTemp,
            tomorrowProbabilityOfPrecipitation: tomorrowPrecip,
            tomorrowWindSpeedHi: tomorrowWind,
            tomorrowDetailedForecast: tomorrowDetailed,
            hourly: state.hourlyPeriods,
            uvIndex: uv,
            aqi: aqi
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
