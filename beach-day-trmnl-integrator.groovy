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
        section("General Configuration") {
            input "weatherDevice", "capability.sensor", title: "Open-Meteo Weather Enhanced Device", required: true, multiple: false
            input "locationName", "text", title: "Location Name (optional)", required: false
            input "webhookUrl", "text", title: "TRMNL Webhook URL", required: true
            input "logEnable", "bool", title: "Enable debug logging", defaultValue: true
            input name: "btnForceUpdate", type: "button", title: "Force Update Now"
        }
        section("Street Parking Restrictions (Alert replaces Sunset time)") {
            paragraph "<b>Left Side Parking Restriction</b>"
            input "parkingLeftEnabled", "bool", title: "Enable Left-Side Parking Reminder", defaultValue: true
            input "parkingLeftWeeks", "enum", title: "Week(s) of Month", options: ["1": "1st Week", "2": "2nd Week", "3": "3rd Week", "4": "4th Week", "5": "5th Week"], multiple: true, defaultValue: ["1", "3"]
            input "parkingLeftDay", "enum", title: "Day of Week", options: ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"], defaultValue: "Wednesday"
            input "parkingLeftStart", "time", title: "Restriction Start Time", defaultValue: "08:00"
            input "parkingLeftEnd", "time", title: "Restriction End Time", defaultValue: "10:00"

            paragraph "<b>Right Side Parking Restriction</b>"
            input "parkingRightEnabled", "bool", title: "Enable Right-Side Parking Reminder", defaultValue: true
            input "parkingRightWeeks", "enum", title: "Week(s) of Month", options: ["1": "1st Week", "2": "2nd Week", "3": "3rd Week", "4": "4th Week", "5": "5th Week"], multiple: true, defaultValue: ["1", "3"]
            input "parkingRightDay", "enum", title: "Day of Week", options: ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"], defaultValue: "Thursday"
            input "parkingRightStart", "time", title: "Restriction Start Time", defaultValue: "08:00"
            input "parkingRightEnd", "time", title: "Restriction End Time", defaultValue: "10:00"
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

    // Evaluate street parking restriction status
    Map parkingAlert = evaluateParkingAlert(sunsetVal)

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
            airQualityDaylightHrsMinTomorrow: tomorrowAqiMin,
            parking_alert_active: parkingAlert.active,
            parking_alert_text: parkingAlert.text
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

private Map evaluateParkingAlert(int sunsetVal) {
    List<Map> rules = []

    // Left Side Rule
    boolean leftEnabled = (settings.parkingLeftEnabled != null) ? settings.parkingLeftEnabled.toBoolean() :
                          ((settings.parking1Enabled != null) ? settings.parking1Enabled.toBoolean() : true)
    if (leftEnabled) {
        List<Integer> weeks = parseWeeks(settings.parkingLeftWeeks ?: settings.parking1Weeks ?: ["1", "3"])
        String day = settings.parkingLeftDay ?: settings.parking1Day ?: "Wednesday"
        int startMins = parseTimeToMinutes(settings.parkingLeftStart ?: settings.parking1Start ?: "08:00")
        int endMins = parseTimeToMinutes(settings.parkingLeftEnd ?: settings.parking1End ?: "10:00")
        String text = formatParkingText("LEFT", startMins, endMins)
        rules << [side: "LEFT", day: day, weeks: weeks, startMins: startMins, endMins: endMins, displayText: text]
    }

    // Right Side Rule
    boolean rightEnabled = (settings.parkingRightEnabled != null) ? settings.parkingRightEnabled.toBoolean() :
                           ((settings.parking2Enabled != null) ? settings.parking2Enabled.toBoolean() : true)
    if (rightEnabled) {
        List<Integer> weeks = parseWeeks(settings.parkingRightWeeks ?: settings.parking2Weeks ?: ["1", "3"])
        String day = settings.parkingRightDay ?: settings.parking2Day ?: "Thursday"
        int startMins = parseTimeToMinutes(settings.parkingRightStart ?: settings.parking2Start ?: "08:00")
        int endMins = parseTimeToMinutes(settings.parkingRightEnd ?: settings.parking2End ?: "10:00")
        String text = formatParkingText("RIGHT", startMins, endMins)
        rules << [side: "RIGHT", day: day, weeks: weeks, startMins: startMins, endMins: endMins, displayText: text]
    }

    if (rules.isEmpty()) {
        return [active: false, text: ""]
    }

    TimeZone tz = location.timeZone ?: TimeZone.getDefault()
    Calendar now = Calendar.getInstance(tz)

    int todayDayOfWeek = now.get(Calendar.DAY_OF_WEEK)
    int todayDayOfMonth = now.get(Calendar.DAY_OF_MONTH)
    int todayWeekOfMonth = ((todayDayOfMonth - 1) / 7).intValue() + 1
    int currentMinsFromMidnight = now.get(Calendar.HOUR_OF_DAY) * 60 + now.get(Calendar.MINUTE)

    Calendar tomorrow = (Calendar) now.clone()
    tomorrow.add(Calendar.DAY_OF_MONTH, 1)
    int tomorrowDayOfWeek = tomorrow.get(Calendar.DAY_OF_WEEK)
    int tomorrowDayOfMonth = tomorrow.get(Calendar.DAY_OF_MONTH)
    int tomorrowWeekOfMonth = ((tomorrowDayOfMonth - 1) / 7).intValue() + 1

    int effectiveSunsetMins = (sunsetVal > 0) ? sunsetVal : (19 * 60 + 30)

    Map dayOfWeekMap = [
        "Sunday": 1, "Monday": 2, "Tuesday": 3, "Wednesday": 4,
        "Thursday": 5, "Friday": 6, "Saturday": 7
    ]

    for (Map r in rules) {
        Integer targetDayNum = dayOfWeekMap[r.day]
        if (!targetDayNum) continue

        // Case 1: Today IS the restriction day (active from sunset night before until endMins today)
        if (todayDayOfWeek == targetDayNum && r.weeks.contains(todayWeekOfMonth)) {
            if (currentMinsFromMidnight <= r.endMins) {
                return [active: true, text: r.displayText]
            }
        }

        // Case 2: Today IS the night BEFORE the restriction day (active from sunset today onwards)
        if (tomorrowDayOfWeek == targetDayNum && r.weeks.contains(tomorrowWeekOfMonth)) {
            if (currentMinsFromMidnight >= effectiveSunsetMins) {
                return [active: true, text: r.displayText]
            }
        }
    }

    return [active: false, text: ""]
}

private String formatParkingText(String side, int startMins, int endMins) {
    String startStr = formatTimeShort(startMins)
    String endStr = formatTimeShort(endMins)

    if (startStr.endsWith("AM") && endStr.endsWith("AM")) {
        startStr = startStr.replaceAll("AM", "")
    } else if (startStr.endsWith("PM") && endStr.endsWith("PM")) {
        startStr = startStr.replaceAll("PM", "")
    }

    return "NO PARKING ${side} SIDE: ${startStr}-${endStr}"
}

private String formatTimeShort(int totalMinutes) {
    int h = (totalMinutes / 60).intValue() % 24
    int m = totalMinutes % 60
    String ampm = h >= 12 ? "PM" : "AM"
    int displayH = h % 12
    if (displayH == 0) displayH = 12
    if (m == 0) {
        return "${displayH}${ampm}"
    } else {
        String displayM = m < 10 ? "0${m}" : "${m}"
        return "${displayH}:${displayM}${ampm}"
    }
}

private List<Integer> parseWeeks(Object val) {
    if (val == null) return [1, 3]
    List<Integer> res = []
    if (val instanceof List) {
        val.each { item ->
            String s = item.toString().trim()
            if (s.isInteger()) res << s.toInteger()
        }
    } else {
        val.toString().split(",").each { String s ->
            String trimmed = s.trim()
            if (trimmed.isInteger()) res << trimmed.toInteger()
        }
    }
    return res.isEmpty() ? [1, 3] : res
}

private int parseTimeToMinutes(Object val) {
    if (val == null) return 0
    String str = val.toString().trim()
    if (!str) return 0

    if (str.contains("T")) {
        str = str.split("T")[1]
    }
    if (str.contains("-") && !str.startsWith("-")) {
        str = str.split("-")[0]
    }
    if (str.contains("+")) {
        str = str.split("\\+")[0]
    }

    boolean isPM = str.toUpperCase().contains("PM")
    boolean isAM = str.toUpperCase().contains("AM")
    str = str.replaceAll("(?i)[a-z\\s]", "")

    try {
        def parts = str.split(":")
        if (parts.size() >= 2) {
            int h = parts[0].toInteger()
            int m = parts[1].toInteger()
            if (isPM && h < 12) h += 12
            if (isAM && h == 12) h = 0
            return h * 60 + m
        }
    } catch (e) {
        log.error "Failed to parse time string '${val}': ${e.message}"
    }
    return 0
}
