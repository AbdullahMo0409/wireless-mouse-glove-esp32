#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BleMouse.h>

constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;

constexpr uint8_t CLICK_AXIS = 0;
constexpr uint8_t SCROLL_AXIS = 1;

constexpr bool POSITIVE_CLICK_IS_LEFT = true;
constexpr bool POSITIVE_SCROLL_IS_UP = true; 

constexpr bool POINTER_SWAP_XY = false;
constexpr int POINTER_X_DIRECTION = +1;     
constexpr int POINTER_Y_DIRECTION = +1;     

constexpr uint32_t SAMPLE_INTERVAL_MS = 15;
constexpr float ACCEL_FILTER_ALPHA = 0.18f;
constexpr float CURSOR_DEAD_ZONE = 0.70f; 
constexpr float CURSOR_SENSITIVITY = 2.2f;
constexpr int MAX_CURSOR_SPEED = 23;     
constexpr float MAX_CURSOR_TILT_DEG = 86.0f;

constexpr float SNAP_TRIGGER_DEG = 70.0f;
constexpr float SNAP_START_RATE_DPS = 155.0f;   
constexpr uint32_t SNAP_MAX_DURATION_MS = 630; 
constexpr float SNAP_START_NEUTRAL_DEG = 33.0f; 

constexpr float REARM_NEUTRAL_DEG = 23.0f;      
constexpr float REARM_MAX_RATE_DPS = 60.0f;    
constexpr uint32_t REARM_STABLE_MS = 120;

constexpr int8_t SCROLL_STEPS_PER_SNAP = 3;     
constexpr bool DEBUG_MOTION = false;           

Adafruit_MPU6050 mpu;
BleMouse bleMouse("Glove Air Mouse", "ESP32 Glove", 100);

enum class GestureState : uint8_t {
  READY,          
  MEASURING,      
  WAIT_FOR_CENTER 
};

enum class GestureAxis : uint8_t { CLICK, SCROLL };

GestureState gestureState = GestureState::READY;
GestureAxis activeAxis = GestureAxis::CLICK;

float gFiltered[3] = {0.0f, 0.0f, 9.81f};
float gNeutral[3] = {0.0f, 0.0f, 9.81f};
float gyroBias[3] = {0.0f, 0.0f, 0.0f}; 

float turnDegrees = 0.0f;
uint32_t gestureStartMs = 0;
uint32_t centerSinceMs = 0;
uint32_t lastSampleMs = 0;
uint32_t lastDebugMs = 0;

float absoluteFloat(float x) { return fabsf(x); }

float bounded(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}


float gyroRateDps(const sensors_event_t &g, uint8_t axis) {
  float measured = 0.0f;
  if (axis == 0) measured = g.gyro.x;
  else if (axis == 1) measured = g.gyro.y;
  else measured = g.gyro.z;
  return (measured - gyroBias[axis]) * RAD_TO_DEG;
}

float tiltFromNeutralDegrees() {
  const float dot = gFiltered[0]*gNeutral[0] +
                    gFiltered[1]*gNeutral[1] +
                    gFiltered[2]*gNeutral[2];
  const float normA = sqrtf(gFiltered[0]*gFiltered[0] +
                            gFiltered[1]*gFiltered[1] +
                            gFiltered[2]*gFiltered[2]);
  const float normB = sqrtf(gNeutral[0]*gNeutral[0] +
                            gNeutral[1]*gNeutral[1] +
                            gNeutral[2]*gNeutral[2]);
  if (normA < 0.1f || normB < 0.1f) return 0.0f;
  const float cosine = bounded(dot / (normA * normB), -1.0f, 1.0f);
  return acosf(cosine) * RAD_TO_DEG;
}

int8_t pixelsFromTilt(float gravityDifference) {
  if (absoluteFloat(gravityDifference) < CURSOR_DEAD_ZONE) return 0;
  const float excess = absoluteFloat(gravityDifference) - CURSOR_DEAD_ZONE;
  int result = (int)roundf(excess * CURSOR_SENSITIVITY);
  if (result > MAX_CURSOR_SPEED) result = MAX_CURSOR_SPEED;
  return (int8_t)(gravityDifference > 0.0f ? result : -result);
}

void sendGesture(GestureAxis axis, float signedAngle) {
  if (!bleMouse.isConnected()) {
    Serial.println("Gesture recognized; Bluetooth not connected");
    return;
  }

  const bool positive = signedAngle > 0.0f;

  if (axis == GestureAxis::CLICK) {
    const bool leftClick = (positive == POSITIVE_CLICK_IS_LEFT);
    bleMouse.click(leftClick ? MOUSE_LEFT : MOUSE_RIGHT);
    Serial.println(leftClick ? "LEFT CLICK" : "RIGHT CLICK");
  } else {
    const bool upward = (positive == POSITIVE_SCROLL_IS_UP);
    const int8_t wheel = upward ? SCROLL_STEPS_PER_SNAP : -SCROLL_STEPS_PER_SNAP;
    bleMouse.move(0, 0, wheel);
    Serial.println(upward ? "SCROLL UP" : "SCROLL DOWN");
  }
}

void updateGesture(uint32_t now, float dtSeconds,
                   float clickRate, float scrollRate, float tiltDegrees) {
  if (gestureState == GestureState::WAIT_FOR_CENTER) {
    const bool nearCenter = tiltDegrees < REARM_NEUTRAL_DEG;
    const bool slowEnough = absoluteFloat(clickRate) < REARM_MAX_RATE_DPS &&
                            absoluteFloat(scrollRate) < REARM_MAX_RATE_DPS;
    if (nearCenter && slowEnough) {
      if (centerSinceMs == 0) centerSinceMs = now;
      else if (now - centerSinceMs >= REARM_STABLE_MS) {
        gestureState = GestureState::READY;
        centerSinceMs = 0;
      }
    } else {
      centerSinceMs = 0;
    }
    return;
  }

  if (gestureState == GestureState::READY) {
    if (tiltDegrees > SNAP_START_NEUTRAL_DEG) return;

    const float clickMagnitude = absoluteFloat(clickRate);
    const float scrollMagnitude = absoluteFloat(scrollRate);
    const float larger = (clickMagnitude > scrollMagnitude)
                           ? clickMagnitude : scrollMagnitude;
    if (larger < SNAP_START_RATE_DPS) return;

    activeAxis = (clickMagnitude >= scrollMagnitude)
                   ? GestureAxis::CLICK : GestureAxis::SCROLL;
    gestureState = GestureState::MEASURING;
    gestureStartMs = now;
    turnDegrees = 0.0f;
  }

  if (gestureState == GestureState::MEASURING) {
  
    const float rate = (activeAxis == GestureAxis::CLICK) ? clickRate : scrollRate;
    turnDegrees += rate * dtSeconds;

    if (absoluteFloat(turnDegrees) >= SNAP_TRIGGER_DEG &&
        now - gestureStartMs <= SNAP_MAX_DURATION_MS) {
      sendGesture(activeAxis, turnDegrees);
      gestureState = GestureState::WAIT_FOR_CENTER;
      centerSinceMs = 0;
    } else if (now - gestureStartMs > SNAP_MAX_DURATION_MS) {
    
      gestureState = GestureState::WAIT_FOR_CENTER;
      centerSinceMs = 0;
      Serial.println("Slow/partial rotation: no gesture");
    }
  }
}

void movePointer(float tiltDegrees) {
  if (gestureState != GestureState::READY ||
      tiltDegrees > MAX_CURSOR_TILT_DEG || !bleMouse.isConnected()) return;

  float horizontal = gFiltered[1] - gNeutral[1];
  float vertical = gFiltered[0] - gNeutral[0];
  if (POINTER_SWAP_XY) {
    const float swapValue = horizontal;
    horizontal = vertical;
    vertical = swapValue;
  }

  horizontal *= POINTER_X_DIRECTION;
  vertical *= POINTER_Y_DIRECTION;

  const int8_t dx = pixelsFromTilt(horizontal);
  const int8_t dy = pixelsFromTilt(vertical);
  if (dx != 0 || dy != 0) bleMouse.move(dx, dy);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!mpu.begin(0x68) && !mpu.begin(0x69)) {
    Serial.println("MPU6050 NOT FOUND: check power and I2C wiring");
    while (true) delay(1000);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("Keep the glove still at your normal pointing position...");
  constexpr int CALIBRATION_SAMPLES = 170;
  float accelSum[3] = {0.0f, 0.0f, 0.0f};
  float gyroSum[3] = {0.0f, 0.0f, 0.0f};

  for (int i = 0; i < CALIBRATION_SAMPLES; ++i) {
    sensors_event_t a, g, temperature;
    if (!mpu.getEvent(&a, &g, &temperature)) {
      Serial.println("MPU6050 failed during calibration");
      while (true) delay(1000);
    }
    accelSum[0] += a.acceleration.x;
    accelSum[1] += a.acceleration.y;
    accelSum[2] += a.acceleration.z;
    gyroSum[0] += g.gyro.x;
    gyroSum[1] += g.gyro.y;
    gyroSum[2] += g.gyro.z;
    delay(10);
  }

  for (uint8_t i = 0; i < 3; ++i) {
    gNeutral[i] = accelSum[i] / CALIBRATION_SAMPLES;
    gFiltered[i] = gNeutral[i];
    gyroBias[i] = gyroSum[i] / CALIBRATION_SAMPLES;
  }

  bleMouse.begin();
  lastSampleMs = millis();
  Serial.println("READY. Bluetooth device name: Glove Air Mouse");
  Serial.println("Slow tilts move. Fast side twists click. Fast pitch turns scroll.");
}

void loop() {
  const uint32_t now = millis();
  const uint32_t elapsedMs = now - lastSampleMs;
  if (elapsedMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;
  const float dt = bounded(elapsedMs * 0.001f, 0.001f, 0.06f);

  sensors_event_t a, g, temperature;
  if (!mpu.getEvent(&a, &g, &temperature)) return;

  gFiltered[0] += ACCEL_FILTER_ALPHA * (a.acceleration.x - gFiltered[0]);
  gFiltered[1] += ACCEL_FILTER_ALPHA * (a.acceleration.y - gFiltered[1]);
  gFiltered[2] += ACCEL_FILTER_ALPHA * (a.acceleration.z - gFiltered[2]);

  const float clickRate = gyroRateDps(g, CLICK_AXIS);
  const float scrollRate = gyroRateDps(g, SCROLL_AXIS);
  const float tiltDegrees = tiltFromNeutralDegrees();

  updateGesture(now, dt, clickRate, scrollRate, tiltDegrees);
  movePointer(tiltDegrees);

  if (DEBUG_MOTION && now - lastDebugMs > 200) {
    Serial.print("Tilt="); Serial.print(tiltDegrees, 1);
    Serial.print(" deg, click gyro="); Serial.print(clickRate, 1);
    Serial.print(" dps, scroll gyro="); Serial.print(scrollRate, 1);
    Serial.print(" dps, state="); Serial.println((int)gestureState);
    lastDebugMs = now;
  }
}
