#include "splash.h"
#include <Arduino.h>

void drawSplashCube(Adafruit_SH1106G &display) {
  // Cube vertices (unit cube centered at origin)
  static const float cubeVerts[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
  };
  // 12 edges as vertex index pairs
  static const uint8_t cubeEdges[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7}
  };
  // "H" on right face (x=+1 plane) — visible first
  static const float letterH[3][6] = {
    {1.01f,-0.6f, 0.5f, 1.01f, 0.6f, 0.5f},
    {1.01f,-0.6f,-0.5f, 1.01f, 0.6f,-0.5f},
    {1.01f, 0.0f, 0.5f, 1.01f, 0.0f,-0.5f}
  };
  // "C" on front face (z=+1 plane) — visible second
  static const float letterC[3][6] = {
    {-0.5f,-0.6f, 1.01f,  0.5f,-0.6f, 1.01f},
    {-0.5f, 0.6f, 1.01f,  0.5f, 0.6f, 1.01f},
    { 0.5f,-0.6f, 1.01f,  0.5f, 0.6f, 1.01f}
  };

  const float scale = 52.0f;
  const float focal = 90.0f;
  const float camDist = 6.0f;
  const int cx = 64, cy = 32;

  auto thickLine = [&](int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    display.drawLine(x0, y0, x1, y1, SH110X_WHITE);
    int16_t dx = x1 - x0, dy = y1 - y0;
    if (abs(dx) > abs(dy)) {
      display.drawLine(x0, y0+1, x1, y1+1, SH110X_WHITE);
    } else {
      display.drawLine(x0+1, y0, x1+1, y1, SH110X_WHITE);
    }
  };

  const float cosT = 0.7071f;
  const float sinT = 0.7071f;

  // Precomputed sin/cos lookup (64 steps per full rotation)
  static const int8_t sinTab[64] = {
     0,  6, 12, 18, 24, 30, 36, 42, 47, 51, 55, 59, 62, 64, 66, 67,
    67, 67, 66, 64, 62, 59, 55, 51, 47, 42, 36, 30, 24, 18, 12,  6,
     0, -6,-12,-18,-24,-30,-36,-42,-47,-51,-55,-59,-62,-64,-66,-67,
   -67,-67,-66,-64,-62,-59,-55,-51,-47,-42,-36,-30,-24,-18,-12, -6
  };

  unsigned long splashStart = millis();
  uint8_t angleStep = 4;

  while (millis() - splashStart < 2000) {
    float sinA = sinTab[angleStep & 63] / 67.0f;
    float cosA = sinTab[(angleStep + 16) & 63] / 67.0f;

    int16_t px[8], py[8];
    for (uint8_t i = 0; i < 8; i++) {
      float rx = cubeVerts[i][0]*cosA - cubeVerts[i][2]*sinA;
      float ry = cubeVerts[i][1];
      float rz = cubeVerts[i][0]*sinA + cubeVerts[i][2]*cosA;
      float ty = ry*cosT - rz*sinT;
      float tz = ry*sinT + rz*cosT;
      float invZ = focal / (tz * scale / focal + camDist);
      px[i] = cx + (int16_t)(rx * scale * invZ / focal);
      py[i] = cy + (int16_t)(ty * scale * invZ / focal);
    }

    display.clearDisplay();

    for (uint8_t i = 0; i < 12; i++) {
      thickLine(px[cubeEdges[i][0]], py[cubeEdges[i][0]],
                px[cubeEdges[i][1]], py[cubeEdges[i][1]]);
    }

    float nzFront = cosA * cosT;
    float nzRight = -sinA * cosT;

    auto project = [&](float ix, float iy, float iz, int16_t &sx, int16_t &sy) {
      float rx = ix*cosA - iz*sinA;
      float ry = iy;
      float rz = ix*sinA + iz*cosA;
      float ty = ry*cosT - rz*sinT;
      float tz = ry*sinT + rz*cosT;
      float invZ = focal / (tz * scale / focal + camDist);
      sx = cx + (int16_t)(rx * scale * invZ / focal);
      sy = cy + (int16_t)(ty * scale * invZ / focal);
    };

    if (nzRight < -0.1f) {
      for (uint8_t i = 0; i < 3; i++) {
        int16_t sx0, sy0, sx1, sy1;
        project(letterH[i][0], letterH[i][1], letterH[i][2], sx0, sy0);
        project(letterH[i][3], letterH[i][4], letterH[i][5], sx1, sy1);
        thickLine(sx0, sy0, sx1, sy1);
      }
    }

    if (nzFront < -0.1f) {
      for (uint8_t i = 0; i < 3; i++) {
        int16_t sx0, sy0, sx1, sy1;
        project(letterC[i][0], letterC[i][1], letterC[i][2], sx0, sy0);
        project(letterC[i][3], letterC[i][4], letterC[i][5], sx1, sy1);
        thickLine(sx0, sy0, sx1, sy1);
      }
    }

    display.display();
    angleStep++;
  }
}
