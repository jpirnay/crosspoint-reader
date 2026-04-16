#include "QmiTestActivity.h"

#include <Arduino.h>
#include <Wire.h>

#include <cstdio>

#include "HalGPIO.h"
#include "I18n.h"
#include "Logging.h"
#include "components/UITheme.h"
#include "fontIds.h"

#ifndef I2C_SDA
#define I2C_SDA X3_I2C_SDA
#endif

#ifndef I2C_SCL
#define I2C_SCL X3_I2C_SCL
#endif

#ifndef QMI8658_L_ADDR
#define QMI8658_L_ADDR I2C_ADDR_QMI8658
#endif

void QmiTestActivity::onEnter() {
  Activity::onEnter();

  qmiAddr = X3GPIO::initQMI8658();
  sensorFound = qmiAddr != 0;
  if (!sensorFound) {
    LOG_ERR("QMI", "QMI8658 not found");
  }

  requestUpdate();
}

void QmiTestActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (sensorFound) {
    const bool okAccX = X3GPIO::readQMI8658AccelAxis(qmiAddr, QMI8658_ACCEL_X_L, &rawAccX);
    const bool okAccY = X3GPIO::readQMI8658AccelY(qmiAddr, &rawAccY);
    const bool okAccZ = X3GPIO::readQMI8658AccelAxis(qmiAddr, QMI8658_ACCEL_Z_L, &rawAccZ);
    const bool okGyroX = X3GPIO::readQMI8658GyroX(qmiAddr, &rawGyroX);
    const bool okGyroY = X3GPIO::readQMI8658GyroY(qmiAddr, &rawGyroY);
    const bool okGyroZ = X3GPIO::readQMI8658GyroZ(qmiAddr, &rawGyroZ);
    if (okAccX && okAccY && okAccZ && okGyroX && okGyroY && okGyroZ) {
      const float accScale = 4.0f / 32768.0f;
      const float gyroScale = 512.0f / 32768.0f;

      float ax = rawAccX * accScale;
      float ay = rawAccY * accScale;
      float az = rawAccZ * accScale;

      float gx = (rawGyroX - gyroBiasX) * gyroScale;
      float gy = (rawGyroY - gyroBiasY) * gyroScale;
      float gz = rawGyroZ * gyroScale;

      accX = ax;
      accY = ay;
      accZ = az;
      gyroX = gx;
      gyroY = gy;
      gyroZ = gz;

      // --- Angle calculation (Accelerometer) ---
      // Roll: Rotation around the X-axis
      // Pitch: Rotation around the Y-axis
      float accRoll = atan2(ay, sqrt(ax * ax + az * az)) * 57.29578f;
      float accPitch = atan2(-ax, sqrt(ay * ay + az * az)) * 57.29578f;

      // Zeitberechnung
      uint32_t now = micros();
      float dt = (float)(now - lastMicros) / 1000000.0f;
      // Falls dt > 150ms (Display hat gerendert), Filter "resetten"
      if (dt > 0.15f) {
        dt = 0.008f;                   // Standard-Intervall simulieren
        kalmanRoll.setAngle(accRoll);  // Filter hart auf Acc-Winkel setzen
        kalmanPitch.setAngle(accPitch);
      }
      lastMicros = now;

      // --- Apply Kalman-filter ---
      float stableRoll = kalmanRoll.update(accRoll, gx, dt);
      float stablePitch = kalmanPitch.update(accPitch, gy, dt);

      rollVelocity = (stableRoll - lastRoll) / dt;
      pitchVelocity = (stablePitch - lastPitch) / dt;

      lastRoll = stableRoll;
      lastPitch = stablePitch;

    } else {
      sensorFound = false;
      LOG_ERR("QMI", "QMI8658 read failed");
    }
    requestUpdate();
  }
}

void QmiTestActivity::onExit() {
  if (qmiAddr) {
    X3GPIO::shutdownQMI8658(qmiAddr);
  }
  sensorFound = false;
  qmiAddr = 0;
  Activity::onExit();
}

void QmiTestActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight}, "QMI8658");

  char buffer[80];
  int y = contentRect.y + metrics.headerHeight + 10;
  const int lineHeight = 22;

  std::snprintf(buffer, sizeof(buffer), "Status: %s", sensorFound ? "Found" : "Not found");
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Accel X: %.2f", accX);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Accel Y: %.2f", accY);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Accel Z: %.2f", accZ);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  /*
  std::snprintf(buffer, sizeof(buffer), "Accel RAW X: %d", rawAccX);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Accel RAW Y: %d", rawAccY);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Accel RAW Z: %d", rawAccZ);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  */
  std::snprintf(buffer, sizeof(buffer), "Gyro X: %.2f", gyroX);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Gyro Y: %.2f", gyroY);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Gyro Z: %.2f", gyroZ);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  /*
  std::snprintf(buffer, sizeof(buffer), "Gyro RAW X: %d", rawGyroX);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Gyro RAW Y: %d", rawGyroY);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  std::snprintf(buffer, sizeof(buffer), "Gyro RAW Z: %d", rawGyroZ);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  */
  std::snprintf(buffer, sizeof(buffer), "QMI Addr: 0x%02X", qmiAddr);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  std::snprintf(buffer, sizeof(buffer), "Roll: %.2f", lastRoll);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  std::snprintf(buffer, sizeof(buffer), "Pitch: %.2f", lastPitch);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  // Establish a possible gesture detection threshold (e.g., >30°/s)
  std::snprintf(buffer, sizeof(buffer), "Roll Velocity: %.2f °/s", rollVelocity);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;
  std::snprintf(buffer, sizeof(buffer), "Pitch Velocity: %.2f °/s", pitchVelocity);
  renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, y, buffer);
  y += lineHeight;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
  // Lets avoid large dt values on the first few frames which can cause instability in the Kalman filter. We can also
  // consider capping dt to a maximum value (e.g., 0.1s) to prevent extreme jumps after long pauses or debugging
  // breakpoints.
  lastMicros = micros();
}
