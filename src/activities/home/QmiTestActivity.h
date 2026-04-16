#pragma once

#include <cstdint>
#include "../Activity.h"

class KalmanFilter {
 public:
  KalmanFilter() {
    Q_angle = 0.001f;
    Q_bias = 0.003f;
    R_measure = 0.03f;
    angle = 0.0f;
    bias = 0.0f;
    P[0][0] = 0.0f;
    P[0][1] = 0.0f;
    P[1][0] = 0.0f;
    P[1][1] = 0.0f;
  }

  float update(float newAngle, float newRate, float dt) {
    float rate = newRate - bias;
    angle += dt * rate;

    P[0][0] += dt * (dt * P[1][1] - P[0][1] - P[1][0] + Q_angle);
    P[0][1] -= dt * P[1][1];
    P[1][0] -= dt * P[1][1];
    P[1][1] += Q_bias * dt;

    float S = P[0][0] + R_measure;
    float K0 = P[0][0] / S;
    float K1 = P[1][0] / S;

    float y = newAngle - angle;
    angle += K0 * y;
    bias += K1 * y;

    float P00_temp = P[0][0];
    float P01_temp = P[0][1];

    P[0][0] -= K0 * P00_temp;
    P[0][1] -= K0 * P01_temp;
    P[1][0] -= K1 * P00_temp;
    P[1][1] -= K1 * P01_temp;

    return angle;
  }

  void setAngle(float a) { angle = a; }

 private:
  float Q_angle;
  float Q_bias;
  float R_measure;
  float angle;
  float bias;
  float P[2][2];
};

class QmiTestActivity final : public Activity {
 public:
  explicit QmiTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QMI8658 Test", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

 private:
  KalmanFilter kalmanRoll;
  KalmanFilter kalmanPitch;
  float lastRoll = 0, lastPitch = 0;
  float rollVelocity = 0, pitchVelocity = 0;
  uint32_t lastMicros = 0;

  // Offsets (should be calibrated for best results, but these defaults should at least show some movement)
  float gyroBiasX = 0, gyroBiasY = 0;
  uint8_t qmiAddr = 0;
  bool sensorFound = false;
  float accX = 0.0f;
  float accY = 0.0f;
  float accZ = 0.0f;
  float gyroX = 0.0f;
  float gyroY = 0.0f;
  float gyroZ = 0.0f;
  int16_t rawAccX = 0;
  int16_t rawAccY = 0;
  int16_t rawAccZ = 0;
  int16_t rawGyroX = 0;
  int16_t rawGyroY = 0;
  int16_t rawGyroZ = 0;
};
