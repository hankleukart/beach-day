/**
 *  MIT License
 *  Copyright 2026 Hank Leukart
 *  Adapted from a driver originally written by:
 *  Copyright 2023 Daniel Winks (daniel.winks@gmail.com)
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 **/

import com.hubitat.app.ChildDeviceWrapper
import com.hubitat.app.DeviceWrapper
import com.hubitat.app.exception.UnknownDeviceTypeException
import com.hubitat.app.InstalledAppWrapper
import com.hubitat.app.ParentDeviceWrapper
import com.hubitat.hub.domain.Event
import com.hubitat.hub.domain.Location
import groovy.json.JsonOutput
import groovy.json.JsonSlurper
import groovy.transform.CompileStatic
import groovy.transform.Field
import groovy.util.slurpersupport.GPathResult
import groovy.xml.XmlUtil
import hubitat.device.HubResponse
import hubitat.scheduling.AsyncResponse
import java.time.Duration
import java.time.Instant
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.Semaphore
import java.math.RoundingMode

library(
  name: 'UtilitiesAndLoggingLibrary',
  namespace: 'hankle',
  author: 'Hank Leukart',
  description: 'Utility and Logging Library',
  importUrl: ''
)
if (device != null) {
  preferences {
    input 'logEnable', 'bool', title: 'Enable Logging', required: false, defaultValue: true
    input 'debugLogEnable', 'bool', title: 'Enable debug logging', required: false, defaultValue: true
    input 'traceLogEnable', 'bool', title: 'Enable trace logging', required: false, defaultValue: false
    input 'descriptionTextEnable', 'bool', title: 'Enable descriptionText logging', required: false, defaultValue: false
  }
}

@Field static final String USER_AGENT = 'Hubitat-NWS-Forecast-Driver/1.0 (contact: admin@hubitat.local)'

String dniOrAppId(DeviceWrapper dev = null)
{
  if(dev) {return dev.getDeviceNetworkId()}
  return device?.getDeviceNetworkId() ?: app.getId()
}

void clearAllStates() {
  state.clear()
  if (device) device.getCurrentStates().each { device.deleteCurrentState(it.name) }
}

void deleteChildDevices() {
  List<ChildDeviceWrapper> children = getChildDevices()
  children.each { child ->
    deleteChildDevice(child.getDeviceNetworkId())
  }
}

void installed() {
  logDebug('Installed...')
  try {
    initialize()
  } catch(e) {
    logWarn("No initialize() method defined or initialize() resulted in error: ${e}")
  }

  if (settings.logEnable) { runIn(1800, 'logsOff') }
  if (settings.debugLogEnable) { runIn(1800, 'debugLogsOff') }
  if (settings.traceLogEnable) { runIn(1800, 'traceLogsOff') }
}

void uninstalled() {
  logDebug('Uninstalled...')
  unschedule()
  deleteChildDevices()
}

void updated() {
  logDebug('Updated...')
  try { configure() }
  catch(e) {
    logWarn("No configure() method defined or configure() resulted in error: ${e}")
  }
}

void logException(message) {
  if (settings.logEnable) {
    if(device) log.exception "${device.label ?: device.name }: ${message}"
    if(app) log.exception "${app.label ?: app.name }: ${message}"
  }
}

void logError(message) {
  if (settings.logEnable) {
    if(device) log.error "${device.label ?: device.name }: ${message}"
    if(app) log.error "${app.label ?: app.name }: ${message}"
  }
}

void logWarn(message) {
  if (settings.logEnable) {
    if(device) log.warn "${device.label ?: device.name }: ${message}"
    if(app) log.warn "${app.label ?: app.name }: ${message}"
  }
}

void logInfo(message) {
  if (settings.logEnable) {
    if(device) log.info "${device.label ?: device.name }: ${message}"
    if(app) log.info "${app.label ?: app.name }: ${message}"
  }
}

void logDebug(message) {
  if (settings.logEnable && settings.debugLogEnable) {
    if(device) log.debug "${device.label ?: device.name }: ${message}"
    if(app) log.debug "${app.label ?: app.name }: ${message}"
  }
}

void logTrace(message) {
  if (settings.logEnable && settings.traceLogEnable) {
    if(device) log.trace "${device.label ?: device.name }: ${message}"
    if(app) log.trace "${app.label ?: app.name }: ${message}"
  }
}

void logClass(obj) {
  logDebug("Object Class Name: ${getObjectClassName(obj)}")
}

void logXml(GPathResult xml) {
  String serialized = XmlUtil.serialize(xml)
  logDebug(serialized.replace('"', '&quot;').replace("'", '&apos;').replace('<', '&lt;').replace('>','&gt;').replace('&','&amp;'))
}

void logJson(Map message) {
  logDebug(prettyJson(message))
}

void logErrorXml(GPathResult xml) {
  String serialized = XmlUtil.serialize(xml)
  logError(serialized.replace('"', '&quot;').replace("'", '&apos;').replace('<', '&lt;').replace('>','&gt;').replace('&','&amp;'))
}

void logErrorJson(Map message) {
  logError(prettyJson(message))
}

void logsOff() {
  if (device) {
    logWarn("Logging disabled for ${device}")
    device.updateSetting('logEnable', [value: 'false', type: 'bool'] )
  }
  if (app) {
    logWarn("Logging disabled for ${app}")
    app.updateSetting('logEnable', [value: 'false', type: 'bool'] )
  }
}

void debugLogsOff() {
  if (device) {
    logWarn("Debug logging disabled for ${device}")
    device.updateSetting('debugLogEnable', [value: 'false', type: 'bool'] )
  }
  if (app) {
    logWarn("Debug logging disabled for ${app}")
    app.updateSetting('debugLogEnable', [value: 'false', type: 'bool'] )
  }
}

void traceLogsOff() {
  if (device) {
    logWarn("Trace logging disabled for ${device}")
    device.updateSetting('traceLogEnable', [value: 'false', type: 'bool'] )
  }
  if (app) {
    logWarn("Trace logging disabled for ${app}")
    app.updateSetting('traceLogEnable', [value: 'false', type: 'bool'] )
  }
}

@CompileStatic
String prettyJson(Map jsonInput) {
  return JsonOutput.prettyPrint(JsonOutput.toJson(jsonInput))
}

String nowFormatted() {
  if(location.timeZone) return new Date().format('yyyy-MMM-dd h:mm:ss a', location.timeZone)
  else                  return new Date().format('yyyy-MMM-dd h:mm:ss a')
}

@CompileStatic
String runEveryCustomSeconds(Integer minutes) {
  String currentSecond = new Date().format('ss')
  return "${currentSecond} /${minutes} * * * ?"
}

@CompileStatic
String runEveryCustomMinutes(Integer minutes) {
  String currentSecond = new Date().format('ss')
  String currentMinute = new Date().format('mm')
  return "${currentSecond} ${currentMinute}/${minutes} * * * ?"
}

@CompileStatic
String runEveryCustomHours(Integer hours) {
  String currentSecond = new Date().format('ss')
  String currentMinute = new Date().format('mm')
  String currentHour = new Date().format('H')
  return "${currentSecond} ${currentMinute} ${currentHour}/${hours} * * ?"
}

double nowDays() {
  return (now() / 86400000)
}

Integer convertHexToInt(hex) { Integer.parseInt(hex,16) }
String convertHexToIP(hex) {
	[convertHexToInt(hex[0..1]),convertHexToInt(hex[2..3]),convertHexToInt(hex[4..5]),convertHexToInt(hex[6..7])].join(".")
}
String convertIPToHex(String ipAddress) {
  List parts = ipAddress.tokenize('.')
  return String.format("%X%X%X%X", parts[0] as Integer, parts[1] as Integer, parts[2] as Integer, parts[3] as Integer)
}

void tryCreateAccessToken() {
  if (state.accessToken == null) {
    try {
      logDebug('Creating Access Token...')
      createAccessToken()
      logDebug("accessToken: ${state.accessToken}")
    } catch(e) {
      logError('OAuth is not enabled for app. Please enable.')
    }
  }
}

@Field static final String baseUrl = 'https://api.weather.gov'

metadata {
  definition(name: 'NWS Forecast And Alerts', namespace: 'dwinks', author: 'Daniel Winks', importUrl: '', singleThreaded: false) {
    capability 'Sensor'
    capability 'Refresh'

    attribute 'temperature', 'NUMBER'
    attribute 'temperatureHi', 'NUMBER'
    attribute 'temperatureLo', 'NUMBER'
    attribute 'probabilityOfPrecipitation', 'NUMBER'
    attribute 'probabilityOfPrecipitation12h', 'NUMBER'
    attribute 'relativeHumidity', 'NUMBER'
    attribute 'dewpoint', 'NUMBER'
    attribute 'detailedForecast', 'STRING'
    attribute 'detailedForecastLC', 'STRING'
    attribute 'windSpeed', 'STRING'
    attribute 'windSpeedLo', 'NUMBER'
    attribute 'windSpeedHi', 'NUMBER'
    attribute 'windDirection', 'STRING'
    attribute 'lastUpdated', 'STRING'
    attribute 'tomorrowTemperature', 'NUMBER'
    attribute 'tomorrowProbabilityOfPrecipitation', 'NUMBER'
    attribute 'tomorrowWindSpeed', 'STRING'
    attribute 'tomorrowWindSpeedLo', 'NUMBER'
    attribute 'tomorrowWindSpeedHi', 'NUMBER'
    attribute 'tomorrowDetailedForecast', 'STRING'
    attribute 'tomorrowDetailedForecastLC', 'STRING'
    attribute 'weatherPayload', 'STRING'

    attribute 'forecast1h',  'JSON_OBJECT'
    attribute 'forecast2h',  'JSON_OBJECT'
    attribute 'forecast3h',  'JSON_OBJECT'
    attribute 'forecast4h',  'JSON_OBJECT'
    attribute 'forecast5h',  'JSON_OBJECT'
    attribute 'forecast6h',  'JSON_OBJECT'
    attribute 'forecast7h',  'JSON_OBJECT'
    attribute 'forecast8h',  'JSON_OBJECT'
    attribute 'forecast9h',  'JSON_OBJECT'
    attribute 'forecast10h', 'JSON_OBJECT'
    attribute 'forecast11h', 'JSON_OBJECT'
    attribute 'forecast12h', 'JSON_OBJECT'
    attribute 'forecast13h', 'JSON_OBJECT'
    attribute 'forecast14h', 'JSON_OBJECT'
    attribute 'forecast15h', 'JSON_OBJECT'
    attribute 'forecast16h', 'JSON_OBJECT'
    attribute 'forecast17h', 'JSON_OBJECT'
    attribute 'forecast18h', 'JSON_OBJECT'
    attribute 'forecast19h', 'JSON_OBJECT'
    attribute 'forecast20h', 'JSON_OBJECT'
    attribute 'forecast21h', 'JSON_OBJECT'
    attribute 'forecast22h', 'JSON_OBJECT'
    attribute 'forecast23h', 'JSON_OBJECT'
    attribute 'forecast24h', 'JSON_OBJECT'
    attribute 'forecast25h', 'JSON_OBJECT'
    attribute 'forecast26h', 'JSON_OBJECT'
    attribute 'forecast27h', 'JSON_OBJECT'
    attribute 'forecast28h', 'JSON_OBJECT'
    attribute 'forecast29h', 'JSON_OBJECT'
    attribute 'forecast30h', 'JSON_OBJECT'
    attribute 'forecast31h', 'JSON_OBJECT'
    attribute 'forecast32h', 'JSON_OBJECT'
    attribute 'forecast33h', 'JSON_OBJECT'
    attribute 'forecast34h', 'JSON_OBJECT'
    attribute 'forecast35h', 'JSON_OBJECT'
    attribute 'forecast36h', 'JSON_OBJECT'

    command 'initialize'
    command 'refreshDetailedForecast'
    command 'refreshHourlyForecast'
  }

  section() {
    input name: 'latitude', type: 'text', title: 'Latitude', description: '', required:true, defaultValue: location.latitude
    input name: 'longitude', type: 'text', title: 'Longitude', description: '', required:true, defaultValue: location.longitude
    input name: 'updateTime', type: 'enum', title: 'Scheduled Update Time', description: '', required:true,
      options: [[1:'1:00 AM'],[2:'2:00 AM'],[3:'3:00 AM'],[4:'4:00 AM'],[5:'5:00 AM'],[6:'6:00 AM']], defaultValue: 4
    input name: 'hourlyForecastHours', type: 'enum', title: 'Hours of hourly forecast to retrieve', description: '', required:true,
      options: [[0:'None'],[2:'2 Hours'],[4:'4 Hours'],[6:'6 Hours'],[8:'8 Hours'],[12:'12 Hours'],[18:'18 Hours'],[24:'24 Hours'],[36:'36 Hours']], defaultValue: 0
  }
}

void initialize() {
  configure()
}

void configure() {
  unschedule()
  clearAllStates()
  getUri()
  refresh()
  scheduleRefresh()
}

void refresh() {
  getDetailedForecast()
  getHourlyForecast()
}

void refreshDetailedForecast(){
  getDetailedForecast()
  getHourlyForecast()
}

void refreshHourlyForecast(){
  getHourlyForecast()
}

void scheduleRefresh() {
  unschedule()

  // scheduledRefresh = "0 0 ${settings.updateTime} ? * *" // old design
  String scheduledRefresh = "0 0 6,10,14,18,22 ? * *" // changes refresh to be every four hours starting at 6am

  schedule(scheduledRefresh, 'refresh')

  if ((settings.hourlyForecastHours as int) > 0) {
    String hourlyRefresh = '0 55 * ? * *'
    schedule(hourlyRefresh, 'getHourlyForecast')
  }
}

void getDetailedForecast() {
  logDebug('Updating detailed forecast...')
  if (!state.forecastUri) {
    logWarn('forecastUri is not set, skipping detailed forecast update')
    return
  }
  Map params = [
    uri:state.forecastUri,
    requestContentType:'application/json',
    contentType:'application/json',
    headers: ["User-Agent": USER_AGENT]
  ]
  asynchttpGet('getDetailedForecastCallback', params)
}

void getDetailedForecastCallback(AsyncResponse response, Map data = null) {
  logDebug('getDetailedForecastCallback()')
  if(response.hasError()) {
    logError("Error: ${response.getErrorMessage()}")
    runIn(300, 'getDetailedForecast')
    return
  }
  Map jsonData = response.getJson()
  state.units = jsonData?.properties?.units
  List<Map> periods = jsonData?.properties?.periods
  if (!periods) {
    logWarn("No periods found in detailed forecast response")
    return
  }
  Map period = periods.find { it.number == 1 }
  if (!period) {
    logWarn("Period 1 not found in detailed forecast periods")
    return
  }

  String detailedForecast = period['detailedForecast']
  sendEvent(name: 'detailedForecast', value: detailedForecast, descriptionText: 'Updated detailedForecast from NWS')
  
  // Lowercase version for string comparison
  if (detailedForecast) {
    String detailedForecastLC = detailedForecast.toLowerCase()
    sendEvent(name: 'detailedForecastLC', value: detailedForecastLC, descriptionText: 'Updated detailedForecastLC with lowercase letters from NWS')
  }

  // Wind Speed Parsing
  String windSpeed = period['windSpeed']
  sendEvent(name: 'windSpeed', value: windSpeed, descriptionText: 'Updated windSpeed from NWS')

  if (windSpeed == "Calm" || windSpeed == "calm") {
    sendEvent(name: 'windSpeedLo', value: 0, unit: 'mph', descriptionText: 'Updated windSpeedLo from NWS (Calm)')
    sendEvent(name: 'windSpeedHi', value: 0, unit: 'mph', descriptionText: 'Updated windSpeedHi from NWS (Calm)')
  } else if (windSpeed?.contains(' to ')) {
    String cleanWind = windSpeed.replaceAll(/[^0-9\s-to]/, '').trim()
    List<String> parts = cleanWind.split(' to ') as List<String>
    if (parts.size() == 2 && parts[0].trim().isInteger() && parts[1].trim().isInteger()) {
      Integer windSpeedLo = parts[0].trim() as Integer
      Integer windSpeedHi = parts[1].trim() as Integer
      sendEvent(name: 'windSpeedLo', value: windSpeedLo, unit: 'mph', descriptionText: 'Updated windSpeedLo from NWS')
      sendEvent(name: 'windSpeedHi', value: windSpeedHi, unit: 'mph', descriptionText: 'Updated windSpeedHi from NWS')
    }
  } else if (windSpeed) {
    String cleanWind = windSpeed.replaceAll(/[^0-9]/, '').trim()
    if (cleanWind.isInteger()) {
      Integer windSpeedValue = cleanWind as Integer
      sendEvent(name: 'windSpeedLo', value: windSpeedValue, unit: 'mph', descriptionText: 'Updated windSpeedLo from NWS')
      sendEvent(name: 'windSpeedHi', value: windSpeedValue, unit: 'mph', descriptionText: 'Updated windSpeedHi from NWS')
    }
  }

  sendEvent(name: 'temperature', value: period['temperature'], unit: '°' + period.temperatureUnit, descriptionText: 'Updated temperature from NWS')
  sendEvent(name: 'relativeHumidity', value: period['relativeHumidity']?.value ?: 0, unit: '%', descriptionText: 'Updated relativeHumidity from NWS')
  sendEvent(name: 'probabilityOfPrecipitation', value: period['probabilityOfPrecipitation']?.value ?: 0, unit: '%', descriptionText: 'Updated probabilityOfPrecipitation from NWS')
  sendEvent(name: 'dewpoint', value: getDewpoint(period['dewpoint'] as Map, 'us'), unit: '°' + period.temperatureUnit, descriptionText: 'Updated dewpoint from NWS')

  // Extract Tomorrow Period
  Map tomorrowPeriod = null
  if (periods.size() >= 2) {
    if (periods[0]['isDaytime'] == true) {
      if (periods.size() >= 3) {
        tomorrowPeriod = periods[2]
      }
    } else {
      tomorrowPeriod = periods[1]
    }
  }

  if (tomorrowPeriod) {
    String tDetailedForecast = tomorrowPeriod['detailedForecast']
    sendEvent(name: 'tomorrowDetailedForecast', value: tDetailedForecast, descriptionText: 'Updated tomorrowDetailedForecast from NWS')
    if (tDetailedForecast) {
      String tDetailedForecastLC = tDetailedForecast.toLowerCase()
      sendEvent(name: 'tomorrowDetailedForecastLC', value: tDetailedForecastLC, descriptionText: 'Updated tomorrowDetailedForecastLC from NWS')
    }

    // Tomorrow Wind Speed Parsing
    String tWindSpeed = tomorrowPeriod['windSpeed']
    sendEvent(name: 'tomorrowWindSpeed', value: tWindSpeed, descriptionText: 'Updated tomorrowWindSpeed from NWS')

    if (tWindSpeed == "Calm" || tWindSpeed == "calm") {
      sendEvent(name: 'tomorrowWindSpeedLo', value: 0, unit: 'mph', descriptionText: 'Updated tomorrowWindSpeedLo from NWS (Calm)')
      sendEvent(name: 'tomorrowWindSpeedHi', value: 0, unit: 'mph', descriptionText: 'Updated tomorrowWindSpeedHi from NWS (Calm)')
    } else if (tWindSpeed?.contains(' to ')) {
      String cleanWind = tWindSpeed.replaceAll(/[^0-9\s-to]/, '').trim()
      List<String> parts = cleanWind.split(' to ') as List<String>
      if (parts.size() == 2 && parts[0].trim().isInteger() && parts[1].trim().isInteger()) {
        Integer windSpeedLo = parts[0].trim() as Integer
        Integer windSpeedHi = parts[1].trim() as Integer
        sendEvent(name: 'tomorrowWindSpeedLo', value: windSpeedLo, unit: 'mph', descriptionText: 'Updated tomorrowWindSpeedLo from NWS')
        sendEvent(name: 'tomorrowWindSpeedHi', value: windSpeedHi, unit: 'mph', descriptionText: 'Updated tomorrowWindSpeedHi from NWS')
      }
    } else if (tWindSpeed) {
      String cleanWind = tWindSpeed.replaceAll(/[^0-9]/, '').trim()
      if (cleanWind.isInteger()) {
        Integer windSpeedValue = cleanWind as Integer
        sendEvent(name: 'tomorrowWindSpeedLo', value: windSpeedValue, unit: 'mph', descriptionText: 'Updated tomorrowWindSpeedLo from NWS')
        sendEvent(name: 'tomorrowWindSpeedHi', value: windSpeedValue, unit: 'mph', descriptionText: 'Updated tomorrowWindSpeedHi from NWS')
      }
    }

    sendEvent(name: 'tomorrowTemperature', value: tomorrowPeriod['temperature'], unit: '°' + tomorrowPeriod.temperatureUnit, descriptionText: 'Updated tomorrowTemperature from NWS')
    sendEvent(name: 'tomorrowProbabilityOfPrecipitation', value: tomorrowPeriod['probabilityOfPrecipitation']?.value ?: 0, unit: '%', descriptionText: 'Updated tomorrowProbabilityOfPrecipitation from NWS')
  }

  sendEvent(name: 'lastUpdated', value: nowFormatted(), isStateChange: true, descriptionText: 'Updated lastUpdated from NWS detailed forecast callback')
  runIn(2, 'updateWeatherPayload')
}

void getHourlyForecast() {
  logDebug('Updating hourly forecast...')
  if (!state.forecastHourlyUri) {
    logWarn('forecastHourlyUri is not set, skipping hourly forecast update')
    return
  }
  Map params = [
    uri:state.forecastHourlyUri,
    requestContentType:'application/json',
    contentType:'application/json',
    headers: ["User-Agent": USER_AGENT]
  ]
  asynchttpGet('getHourlyForecastCallback', params)
}

void getHourlyForecastCallback(AsyncResponse response, Map data = null) {
  logDebug('getHourlyForecastCallback()')
  if(response.hasError()) {
    logError("Error: ${response.getErrorMessage()}")
    runIn(300, 'getHourlyForecast')
    return
  }
  Map jsonData = response.getJson()
  state.hourlyUnits = jsonData?.properties?.units
  List<Map> periods = jsonData?.properties?.periods

  if (!periods) {
    logWarn("No hourly forecast periods returned from NWS")
    return
  }

  // Calculate high, low temperature and precipitation for the next 12 hours
  int maxPeriods = Math.min(periods.size(), 12)
  if (maxPeriods > 0) {
    List<Map> first12Periods = periods.subList(0, maxPeriods)
    List<Integer> temps = first12Periods.collect { it.temperature as Integer }
    
    // Safety check for probabilityOfPrecipitation values
    List<Integer> probPrecip = first12Periods.collect { Map it ->
      (it.probabilityOfPrecipitation?.value != null) ? (it.probabilityOfPrecipitation.value as Integer) : 0
    }

    sendEvent(name: 'temperatureHi', value: temps.max(), descriptionText: "Hi temp for next 12 hours is ${temps.max()}")
    sendEvent(name: 'temperatureLo', value: temps.min(), descriptionText: "Lo temp for next 12 hours is ${temps.min()}")
    sendEvent(name: 'probabilityOfPrecipitation12h', value: probPrecip.max(), unit: '%', descriptionText: "Updated probabilityOfPrecipitation from NWS for next 12 hours is: ${probPrecip.max()}")
  }

  // Simplify and store hourly forecast periods for the Liquid code (compact format to keep payload < 2KB)
  List<Map> simplifiedPeriods = periods.take(26).collect { Map it ->
    [
      t: (it.startTime && (it.startTime as String).length() >= 13) ? (it.startTime as String).substring(0, 13) : "",
      f: it.shortForecast
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

  List<Map> newFuturePeriods = simplifiedPeriods.findAll { Map it ->
    String t = it.t as String
    return t && t >= currentHourStr
  }

  state.hourlyPeriods = (pastPeriodsToday + newFuturePeriods).take(26)

  // Populate hourly forecast attributes if requested
  int hourlyHours = settings.hourlyForecastHours as int
  if (hourlyHours > 0) {
    (1..hourlyHours).each { int it ->
      if (periods.size() >= it) {
        Map forecast = [:]
        Map period = periods[it - 1]
        forecast.startTime = period['startTime']
        forecast.temperature = period['temperature']
        forecast.temperatureUnit = period['temperatureUnit']
        forecast.windSpeed = period['windSpeed']
        forecast.windDirection = period['windDirection']
        forecast.shortForecast = period['shortForecast']
        forecast.probabilityOfPrecipitation = period['probabilityOfPrecipitation']?.value ?: 0
        forecast.relativeHumidity = period['relativeHumidity']?.value ?: 0
        forecast.dewpoint = getDewpoint(period['dewpoint'] as Map, 'us')

        sendEvent(name: "forecast${it}h", value: forecast, descriptionText: "Updated forecast${it}h from NWS")
      }
    }
  }

  sendEvent(name: 'lastUpdated', value: nowFormatted(), isStateChange: true, descriptionText: 'Updated lastUpdated from NWS hourly forecast callback')
  runIn(2, 'updateWeatherPayload')
}

@CompileStatic
BigDecimal getDewpoint(Map dewpoint, String units) {
  if (dewpoint == null) return 0
  BigDecimal dp = dewpoint.value != null ? dewpoint.value as BigDecimal : 0
  if (units == 'us' && dewpoint.unitCode == 'wmoUnit:degC') {
    return (dp * 1.8 + 32).setScale(0, java.math.RoundingMode.HALF_UP)
  } else {
    return dp.setScale(0, java.math.RoundingMode.HALF_UP)
  }
}

String getUri() {
  Map params = [:]
  params.uri = "${baseUrl}/points/${settings.latitude},${settings.longitude}"
  params.headers = ["User-Agent": USER_AGENT]
  try {
    httpGet(params) { resp ->
      if (resp?.success){
        Map jsonData = parseJson("${resp.getData()}".toString())
        state.forecastUri = jsonData?.properties?.forecast
        state.forecastHourlyUri = jsonData?.properties?.forecastHourly
      } else {
        logWarn "Error: ${resp?.status}"
      }
    }
  } catch (e) {
    logError "getUri failed: ${e}"
  }
}

void updateWeatherPayload() {
  logDebug('Generating weatherPayload payload...')

  // Fetch values from database to check commit status
  def tempHi = device.currentValue('temperatureHi')
  def detailed = device.currentValue('detailedForecast')

  // Self-healing check: if database has not committed yet, wait and try again
  if (tempHi == null || detailed == null) {
    logWarn "Required weather data is not committed to DB yet. Retrying in 2 seconds..."
    runIn(2, 'updateWeatherPayload')
    return
  }

  Map sunTimes = getSunriseAndSunset()
  String sunriseStr = ""
  String sunsetStr = ""
  if (sunTimes && sunTimes.sunrise && sunTimes.sunset) {
    if (location.timeZone) {
      sunriseStr = sunTimes.sunrise.format("MM/dd/yyyy h:mm a", location.timeZone)
      sunsetStr = sunTimes.sunset.format("MM/dd/yyyy h:mm a", location.timeZone)
    } else {
      sunriseStr = sunTimes.sunrise.format("MM/dd/yyyy h:mm a")
      sunsetStr = sunTimes.sunset.format("MM/dd/yyyy h:mm a")
    }
  }

  Map payload = [
    merge_variables: [
      temperatureHi: tempHi,
      probablityofPrecipitation: device.currentValue('probabilityOfPrecipitation'),
      windspeedHi: device.currentValue('windSpeedHi'),
      detailedForecast: detailed,
      sunrise: sunriseStr,
      sunset: sunsetStr,
      tomorrowTemperature: device.currentValue('tomorrowTemperature'),
      tomorrowProbabilityOfPrecipitation: device.currentValue('tomorrowProbabilityOfPrecipitation'),
      tomorrowWindSpeedHi: device.currentValue('tomorrowWindSpeedHi'),
      tomorrowDetailedForecast: device.currentValue('tomorrowDetailedForecast'),
      hourly: state.hourlyPeriods ?: []
    ]
  ]

  String jsonPayload = groovy.json.JsonOutput.toJson(payload)
  sendEvent(name: 'weatherPayload', value: jsonPayload, isStateChange: true, descriptionText: 'Updated weatherPayload payload for TRMNL')
}