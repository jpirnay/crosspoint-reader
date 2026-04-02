#include "WeatherActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WeatherClient.h>
#include <WeatherIcons.h>
#include <WeatherSettingsStore.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "WeatherSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void WeatherActivity::onEnter() {
  Activity::onEnter();

  // Force landscape orientation for weather display
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);

  state = State::LOADING_CACHE;
  errorMessage.clear();
  forceRefresh = false;
  requestUpdate();

  loadAndDisplay();
}

void WeatherActivity::onExit() {
  Activity::onExit();

  // Restore orientation from settings
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(SETTINGS.orientation));

  WiFi.mode(WIFI_OFF);
}

void WeatherActivity::loadAndDisplay() {
  if (!WEATHER_SETTINGS.hasLocation()) {
    state = State::ERROR;
    errorMessage = tr(STR_WEATHER_NO_LOCATION);
    requestUpdate();
    return;
  }

  // Try cache first
  WeatherData cached = WeatherClient::getWeather(WEATHER_SETTINGS, forceRefresh);
  if (cached.valid) {
    weatherData = std::move(cached);
    state = State::WEATHER_DISPLAY;
    requestUpdate();
    return;
  }

  // Need fresh data - check WiFi
  checkAndConnectWifi();
}

void WeatherActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = State::FETCHING;
    requestUpdate(true);
    fetchWeather();
    return;
  }

  launchWifiSelection();
}

void WeatherActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void WeatherActivity::onWifiSelectionComplete(bool connected) {
  // Re-apply landscape after returning from WiFi selection (which uses portrait)
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);

  if (connected) {
    state = State::FETCHING;
    requestUpdate(true);
    fetchWeather();
  } else {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    state = State::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

void WeatherActivity::fetchWeather() {
  weatherData = WeatherClient::getWeather(WEATHER_SETTINGS, true);
  if (weatherData.valid) {
    state = State::WEATHER_DISPLAY;
  } else {
    state = State::ERROR;
    errorMessage = weatherData.errorMessage.empty() ? tr(STR_WEATHER_FETCH_FAILED) : weatherData.errorMessage.c_str();
  }
  requestUpdate();
}

void WeatherActivity::loop() {
  if (state == State::WIFI_SELECTION) {
    return;  // Handled by WifiSelectionActivity
  }

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Retry
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = State::FETCHING;
        requestUpdate(true);
        fetchWeather();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::LOADING_CACHE || state == State::CHECK_WIFI || state == State::FETCHING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  // WEATHER_DISPLAY state
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Open weather settings
    startActivityForResult(std::make_unique<WeatherSettingsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             // Re-apply landscape after returning from settings
                             renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
                             // Refresh after settings change
                             forceRefresh = true;
                             loadAndDisplay();
                           });
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
             mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    // Manual refresh
    forceRefresh = true;
    loadAndDisplay();
  }
}

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();    // 800 in landscape
  const auto pageHeight = renderer.getScreenHeight();  // 480 in landscape

  if (state == State::LOADING_CACHE || state == State::FETCHING) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_LOADING));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // DISPLAY state - Landscape layout (800x480)
  //
  // +------------------+------------------------------------+
  // |  Current weather  |    Daily forecast cards            |
  // |  (icon, temp,     |    [Day1] [Day2] [Day3] ...       |
  // |   details)        |                                    |
  // |  ~200px wide      |    ~600px wide                     |
  // +------------------+------------------------------------+
  // |                                                        |
  // |   48-hour temperature + precipitation graph            |
  // |   ~full width, ~200px high                             |
  // +--------------------------------------------------------+
  // |   [Back]  [Settings]  [Refresh]  Button hints          |
  // +--------------------------------------------------------+

  constexpr int leftPanelWidth = 220;
  constexpr int buttonHintsHeight = 30;
  constexpr int graphHeight = 190;
  const int topSectionHeight = pageHeight - graphHeight - buttonHintsHeight;

  // Draw current conditions (left panel)
  renderCurrentConditions(0, 0, leftPanelWidth, topSectionHeight);

  // Draw daily forecast (right of current conditions)
  renderDailyForecast(leftPanelWidth, 0, pageWidth - leftPanelWidth, topSectionHeight);

  // Separator line
  renderer.drawLine(0, topSectionHeight, pageWidth, topSectionHeight);

  // Draw hourly graph
  renderHourlyGraph(0, topSectionHeight + 1, pageWidth, graphHeight - 1);

  // Separator line above button hints
  renderer.drawLine(0, pageHeight - buttonHintsHeight, pageWidth, pageHeight - buttonHintsHeight);

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_WEATHER_SETTINGS), tr(STR_WEATHER_REFRESH), "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void WeatherActivity::renderCurrentConditions(int x, int y, int w, int h) {
  const auto& cur = weatherData.current;

  // Location name
  int textY = y + 15;
  auto locationName = WEATHER_SETTINGS.getLocationName();
  if (!locationName.empty()) {
    auto truncated = renderer.truncatedText(UI_10_FONT_ID, locationName.c_str(), w - 10);
    renderer.drawText(UI_10_FONT_ID, x + 5, textY, truncated.c_str(), true, EpdFontFamily::BOLD);
    textY += 20;
  }

  // Last updated time
  if (weatherData.fetchedAt > 0) {
    struct tm timeinfo;
    time_t localTime = weatherData.fetchedAt + weatherData.utcOffsetSeconds;
    gmtime_r(&localTime, &timeinfo);
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    renderer.drawText(SMALL_FONT_ID, x + 5, textY, timeBuf);
    textY += 15;
  }

  // Weather icon (48x48)
  auto iconType = getWeatherIconType(cur.weatherCode, cur.isDay);
  const uint8_t* icon = getWeatherIconLarge(iconType);
  int iconX = x + (w - WEATHER_ICON_LARGE) / 2;
  renderer.drawImage(icon, iconX, textY, WEATHER_ICON_LARGE, WEATHER_ICON_LARGE);
  textY += WEATHER_ICON_LARGE + 5;

  // Temperature (large)
  char tempBuf[16];
  const char* unitSuffix = WEATHER_SETTINGS.getTempUnit() == WeatherTempUnit::CELSIUS ? "C" : "F";
  snprintf(tempBuf, sizeof(tempBuf), "%.1f %s", cur.temperature, unitSuffix);
  renderer.drawCenteredText(UI_12_FONT_ID, textY, tempBuf, true, EpdFontFamily::BOLD);
  textY += 22;

  // Feels like
  snprintf(tempBuf, sizeof(tempBuf), "%.1f %s", cur.apparentTemperature, unitSuffix);
  char feelsLikeBuf[48];
  snprintf(feelsLikeBuf, sizeof(feelsLikeBuf), "%s %s", tr(STR_WEATHER_FEELS_LIKE), tempBuf);
  auto feelsText = renderer.truncatedText(SMALL_FONT_ID, feelsLikeBuf, w - 10);
  int feelsWidth = renderer.getTextWidth(SMALL_FONT_ID, feelsText.c_str());
  renderer.drawText(SMALL_FONT_ID, x + (w - feelsWidth) / 2, textY, feelsText.c_str());
  textY += 16;

  // Weather description
  const char* desc = getWeatherDescription(cur.weatherCode);
  int descWidth = renderer.getTextWidth(SMALL_FONT_ID, desc);
  renderer.drawText(SMALL_FONT_ID, x + (w - descWidth) / 2, textY, desc);
  textY += 20;

  // Details grid
  const char* windDir = getWindDirectionText(cur.windDirection);
  char detailBuf[64];

  // Wind
  snprintf(detailBuf, sizeof(detailBuf), "%s: %.0f %s %s", tr(STR_WEATHER_WIND), cur.windSpeed,
           WEATHER_SETTINGS.getWindUnitParam(), windDir);
  renderer.drawText(SMALL_FONT_ID, x + 5, textY, detailBuf);
  textY += 14;

  // Humidity
  snprintf(detailBuf, sizeof(detailBuf), "%s: %d%%", tr(STR_WEATHER_HUMIDITY), cur.humidity);
  renderer.drawText(SMALL_FONT_ID, x + 5, textY, detailBuf);
  textY += 14;

  // Precipitation
  snprintf(detailBuf, sizeof(detailBuf), "%s: %.1f %s", tr(STR_WEATHER_PRECIP), cur.precipitation,
           WEATHER_SETTINGS.getPrecipUnitParam());
  renderer.drawText(SMALL_FONT_ID, x + 5, textY, detailBuf);
  textY += 14;

  // UV Index
  snprintf(detailBuf, sizeof(detailBuf), "UV: %.1f", cur.uvIndex);
  renderer.drawText(SMALL_FONT_ID, x + 5, textY, detailBuf);

  // Vertical separator
  renderer.drawLine(x + w, y, x + w, y + h);
}

void WeatherActivity::renderDailyForecast(int x, int y, int w, int h) {
  if (weatherData.daily.empty()) return;

  int numDays = static_cast<int>(weatherData.daily.size());
  int cardWidth = w / numDays;

  for (int i = 0; i < numDays; i++) {
    const auto& day = weatherData.daily[i];
    int cardX = x + i * cardWidth;
    int textY = y + 10;

    // Day name
    struct tm timeinfo;
    time_t localDate = day.date + weatherData.utcOffsetSeconds;
    gmtime_r(&localDate, &timeinfo);
    char dayName[16];
    // Manual short day names to avoid locale issues on ESP32
    const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(dayName, sizeof(dayName), "%s", dayNames[timeinfo.tm_wday]);

    int dayNameWidth = renderer.getTextWidth(SMALL_FONT_ID, dayName, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, cardX + (cardWidth - dayNameWidth) / 2, textY, dayName, true, EpdFontFamily::BOLD);
    textY += 18;

    // Weather icon (24x24)
    // Use isDay=true for daily forecasts
    auto iconType = getWeatherIconType(day.weatherCode, true);
    const uint8_t* icon = getWeatherIconSmall(iconType);
    int iconX = cardX + (cardWidth - WEATHER_ICON_SMALL) / 2;
    renderer.drawImage(icon, iconX, textY, WEATHER_ICON_SMALL, WEATHER_ICON_SMALL);
    textY += WEATHER_ICON_SMALL + 5;

    // High / Low temp
    char tempBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f/%.0f", day.tempMax, day.tempMin);
    int tempWidth = renderer.getTextWidth(SMALL_FONT_ID, tempBuf);
    renderer.drawText(SMALL_FONT_ID, cardX + (cardWidth - tempWidth) / 2, textY, tempBuf);
    textY += 15;

    // Precipitation
    if (day.precipSum > 0) {
      char precipBuf[16];
      snprintf(precipBuf, sizeof(precipBuf), "%.1f%s", day.precipSum, WEATHER_SETTINGS.getPrecipUnitParam());
      int precipWidth = renderer.getTextWidth(SMALL_FONT_ID, precipBuf);
      renderer.drawText(SMALL_FONT_ID, cardX + (cardWidth - precipWidth) / 2, textY, precipBuf);
    }

    // Card separator
    if (i < numDays - 1) {
      renderer.drawLine(cardX + cardWidth, y, cardX + cardWidth, y + h);
    }
  }
}

void WeatherActivity::renderHourlyGraph(int x, int y, int w, int h) {
  if (weatherData.hourly.empty()) return;

  constexpr int leftMargin = 40;  // Space for Y-axis labels
  constexpr int rightMargin = 10;
  constexpr int topMargin = 15;
  constexpr int bottomMargin = 25;  // Space for X-axis labels
  constexpr int precipBarMaxHeight = 30;

  const int graphX = x + leftMargin;
  const int graphY = y + topMargin;
  const int graphW = w - leftMargin - rightMargin;
  const int graphH = h - topMargin - bottomMargin - precipBarMaxHeight;
  const int precipY = graphY + graphH;

  size_t numPoints = weatherData.hourly.size();
  if (numPoints < 2) return;

  // Find temperature range
  float tempMin = weatherData.hourly[0].temperature;
  float tempMax = tempMin;
  float maxPrecip = 0;
  for (const auto& hr : weatherData.hourly) {
    tempMin = std::min(tempMin, hr.temperature);
    tempMax = std::max(tempMax, hr.temperature);
    maxPrecip = std::max(maxPrecip, hr.precipitation);
  }

  // Add padding to temp range
  float tempRange = tempMax - tempMin;
  if (tempRange < 5.0f) {
    float mid = (tempMax + tempMin) / 2.0f;
    tempMin = mid - 2.5f;
    tempMax = mid + 2.5f;
    tempRange = 5.0f;
  }
  tempMin -= 1.0f;
  tempMax += 1.0f;
  tempRange = tempMax - tempMin;

  // Draw graph title
  renderer.drawText(SMALL_FONT_ID, x + 5, y + 3, tr(STR_WEATHER_48H_FORECAST), true, EpdFontFamily::BOLD);

  // Draw Y-axis labels (temperature)
  float tempStep = tempRange / 4.0f;
  for (int i = 0; i <= 4; i++) {
    float temp = tempMax - i * tempStep;
    int labelY = graphY + i * graphH / 4;
    char label[8];
    snprintf(label, sizeof(label), "%.0f", temp);
    renderer.drawText(SMALL_FONT_ID, x + 2, labelY - 4, label);
    // Grid line (dashed effect using short segments)
    for (int gx = graphX; gx < graphX + graphW; gx += 8) {
      renderer.drawPixel(gx, labelY);
    }
  }

  // Draw temperature line
  int prevPx = -1, prevPy = -1;
  for (size_t i = 0; i < numPoints; i++) {
    int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
    float normalized = (weatherData.hourly[i].temperature - tempMin) / tempRange;
    int py = graphY + graphH - static_cast<int>(normalized * graphH);

    if (prevPx >= 0) {
      renderer.drawLine(prevPx, prevPy, px, py, 2, true);
    }
    prevPx = px;
    prevPy = py;
  }

  // Draw precipitation bars
  if (maxPrecip > 0) {
    for (size_t i = 0; i < numPoints; i++) {
      float precip = weatherData.hourly[i].precipitation;
      if (precip <= 0) continue;

      int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
      int barH = static_cast<int>((precip / maxPrecip) * precipBarMaxHeight);
      if (barH < 1) barH = 1;
      int barW = std::max(1, graphW / static_cast<int>(numPoints) - 1);
      renderer.fillRect(px - barW / 2, precipY + precipBarMaxHeight - barH, barW, barH);
    }
  }

  // Draw X-axis labels (every 6 hours)
  for (size_t i = 0; i < numPoints; i += 6) {
    int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
    struct tm timeinfo;
    time_t localTime = weatherData.hourly[i].time + weatherData.utcOffsetSeconds;
    gmtime_r(&localTime, &timeinfo);
    char label[8];
    snprintf(label, sizeof(label), "%02d:00", timeinfo.tm_hour);
    renderer.drawText(SMALL_FONT_ID, px - 12, precipY + precipBarMaxHeight + 3, label);

    // Vertical grid line
    for (int gy = graphY; gy < precipY + precipBarMaxHeight; gy += 8) {
      renderer.drawPixel(px, gy);
    }
  }

  // Draw axes
  renderer.drawLine(graphX, graphY, graphX, precipY + precipBarMaxHeight);  // Y axis
  renderer.drawLine(graphX, precipY + precipBarMaxHeight, graphX + graphW,
                    precipY + precipBarMaxHeight);  // X axis
}
