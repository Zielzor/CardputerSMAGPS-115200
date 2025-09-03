#include <M5Cardputer.h>    // Main library for M5Stack Cardputer (via M5Unified)
#include <TinyGPSPlus.h>    // Library for parsing GPS NMEA data
#include <SD.h>             // SD card library

// ======== UART pins for Cardputer Grove (default) ========
#define GPS_RX 1            // Cardputer GPIO1  - GPS TX -> this RX
#define GPS_TX 2            // Cardputer GPIO2  - GPS RX <- this TX
// If you rewired, change the defines above accordingly.

// ======== GPS serial @ 115200 ========
HardwareSerial GPS_Serial(1);  // Use UART1 for GPS
TinyGPSPlus gps;

// ======== GPX logging ========
File gpxFile;
bool isRecording = false;
String filename;

// ======== Fix/quality parsed from GGA ========
String lastGGA;
float hdop = 0.0f;   // field 8 in GGA
int   fixQuality = 0; // field 6 in GGA

// ======== Display layout (computed at runtime) ========
int screen_w, screen_h;
const int gap = 4;
int total_w, col0_w, col1_w;
const int field_h = 14;
const int field_gap = 1;

// ---- Parse GGA NMEA sentence for Fix Quality and HDOP ----
// GGA fields (comma-separated):
// 1:UTC, 2:Lat, 3:N/S, 4:Lon, 5:E/W, 6:FixQuality, 7:NumSats, 8:HDOP, 9:Alt, ...
void parseGGA(const String& gga) {
  int field = 0;
  int lastIndex = 0;
  fixQuality = 0;
  hdop = 0.0f;

  for (int i = 0; i < gga.length(); ++i) {
    if (gga[i] == ',' || gga[i] == '*') {
      String value = gga.substring(lastIndex, i);
      lastIndex = i + 1;
      field++;
      if (field == 6) fixQuality = value.toInt();
      if (field == 8) hdop = value.toFloat();
      if (field > 9) break;
    }
  }
}

// ---- Draw a labeled data field (rectangle with label and value) ----
void drawField(int x, int w, int y, const char* label, const char* value) {
  M5Cardputer.Display.drawRect(x, y, w, field_h, TFT_WHITE);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.setCursor(x + 2, y + 3);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.printf("%-5s: %s", label, value);
}

// ---- Create a new GPX file and write the header ----
void createGPX() {
  if (isRecording) return;

  filename = "/track_";
  if (gps.date.isValid()) {
    filename += String(gps.date.year()) + "-";
    filename += String(gps.date.month()) + "-";
    filename += String(gps.date.day()) + "_";
  }
  filename += String(millis() / 1000);
  filename += ".gpx";

  gpxFile = SD.open(filename, FILE_WRITE);
  if (gpxFile) {
    gpxFile.println("<?xml version='1.0' encoding='UTF-8'?>");
    gpxFile.println("<gpx version='1.1'>");
    gpxFile.println("<trk><trkseg>");
    isRecording = true;
  } else {
    // Brief on-screen error if file can't open
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.setCursor(gap, screen_h - 12);
    M5Cardputer.Display.printf("GPX open failed");
  }
}

// ---- Write a single GPX trackpoint with available data ----
void writeGPXPoint() {
  if (!isRecording || !gpxFile) return;
  if (!gps.location.isValid()) return;

  gpxFile.print("<trkpt lat=\"");
  gpxFile.print(gps.location.lat(), 6);
  gpxFile.print("\" lon=\"");
  gpxFile.print(gps.location.lng(), 6);
  gpxFile.println("\">");

  gpxFile.print("<time>");
  if (gps.date.isValid() && gps.time.isValid()) {
    gpxFile.printf("%04d-%02d-%02dT%02d:%02d:%02dZ",
                   gps.date.year(), gps.date.month(), gps.date.day(),
                   gps.time.hour(), gps.time.minute(), gps.time.second());
  }
  gpxFile.println("</time>");

  if (gps.altitude.isValid()) {
    gpxFile.print("<ele>");
    gpxFile.print(gps.altitude.meters());
    gpxFile.println("</ele>");
  }
  if (gps.speed.isValid()) {
    gpxFile.print("<speed>");
    gpxFile.print(gps.speed.kmph());
    gpxFile.println("</speed>");
  }
  if (gps.course.isValid()) {
    gpxFile.print("<course>");
    gpxFile.print(gps.course.deg());
    gpxFile.println("</course>");
  }
  if (hdop > 0.0f) {
    gpxFile.print("<hdop>");
    gpxFile.print(hdop, 1);
    gpxFile.println("</hdop>");
  }
  gpxFile.println("</trkpt>");
  gpxFile.flush();
}

// ---- Finish GPX file and close it ----
void stopRecording() {
  if (isRecording && gpxFile) {
    gpxFile.println("</trkseg></trk></gpx>");
    gpxFile.close();
    isRecording = false;
    M5Cardputer.Display.setCursor(gap, screen_h - 12);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.printf("Saved: %s", filename.c_str());
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setBrightness(32);

  // Compute layout using actual panel size
  screen_w = M5Cardputer.Display.width();
  screen_h = M5Cardputer.Display.height();
  total_w  = screen_w - 3 * gap;
  col0_w   = total_w * 0.5f;
  col1_w   = total_w * 0.5f;

  // SD card init (Cardputer TF slot usually uses CS=4)
  if (!SD.begin(4)) {
    if (!SD.begin()) {
      M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
      M5Cardputer.Display.setCursor(0, 0);
      M5Cardputer.Display.println("SD Card Fail!");
      while (1) { delay(10); }
    }
  }

  // Start GPS UART explicitly at 115200 for your v1.1 module
  GPS_Serial.begin(115200, SERIAL_8N1, GPS_RX, GPS_TX);

  // Optional: give the GPS a moment to start sending at 115200
  delay(200);
}

void loop() {
  M5Cardputer.update();

  // --- Toggle recording with 'r' ---
  static bool rPrev = false;
  bool rNow = M5Cardputer.Keyboard.isKeyPressed('r');
  if (rNow && !rPrev) {
    if (!isRecording) createGPX();
    else              stopRecording();
  }
  rPrev = rNow;

  // --- Read GPS data, feed TinyGPS++, parse GGA for HDOP/Fix Quality ---
  while (GPS_Serial.available()) {
    char c = GPS_Serial.read();
    gps.encode(c);
    static String nmeaLine = "";
    if (c == '\n') {
      if (nmeaLine.startsWith("$GPGGA") || nmeaLine.startsWith("$GNGGA")) {
        lastGGA = nmeaLine;
        parseGGA(lastGGA);
      }
      nmeaLine = "";
    } else if (c != '\r') {
      nmeaLine += c;
    }
  }

  // --- Write GPX point every 3 seconds when recording ---
  static uint32_t lastWrite = 0;
  if (isRecording && (millis() - lastWrite > 3000)) {
    writeGPXPoint();
    lastWrite = millis();
  }

  // --- DISPLAY UPDATE SECTION (every 500 ms) ---
  static uint32_t lastDisplay = 0;
  if (millis() - lastDisplay > 500) {
    lastDisplay = millis();

    M5Cardputer.Display.fillScreen(TFT_BLACK);

    // Header + REC indicator
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5Cardputer.Display.setCursor(gap, 2);
    M5Cardputer.Display.println("GPS Viewer (115200)");

    if (isRecording) {
      M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
      M5Cardputer.Display.setCursor(screen_w - 60, 2);
      M5Cardputer.Display.print("[REC]");
    }

    int y0 = 14;
    int x0 = gap;
    int x1 = gap * 2 + col0_w;

    // First column (Lat, Alt, Date, Qual, Crs)
    char lat[20], alt[20], date[20], qual[20], crs[20];

    if (gps.location.isValid()) sprintf(lat, "%.6f", gps.location.lat());
    else                        sprintf(lat, "NoFix");
    drawField(x0, col0_w, y0, "Lat", lat);

    if (gps.altitude.isValid()) sprintf(alt, "%.2f", gps.altitude.meters());
    else                        sprintf(alt, "NoData");
    drawField(x0, col0_w, y0 + (field_h + field_gap), "Alt", alt);

    if (gps.date.isValid())
      sprintf(date, "%02d/%02d/%02d", gps.date.day(), gps.date.month(), gps.date.year() % 100);
    else
      sprintf(date, "NoData");
    drawField(x0, col0_w, y0 + 2 * (field_h + field_gap), "Date", date);

    sprintf(qual, "%d", fixQuality);
    drawField(x0, col0_w, y0 + 3 * (field_h + field_gap), "Qual", qual);

    if (gps.course.isValid())  sprintf(crs, "%.1f", gps.course.deg());
    else                       sprintf(crs, "NoData");
    drawField(x0, col0_w, y0 + 4 * (field_h + field_gap), "Crs", crs);

    // Second column (Lng, Time, Sat, Spd, HDOP)
    char lng[20], timeStr[20], sats[20], spd[20], hdopStr[20];

    if (gps.location.isValid()) sprintf(lng, "%.6f", gps.location.lng());
    else                        sprintf(lng, "NoFix");
    drawField(x1, col1_w, y0, "Lng", lng);

    if (gps.time.isValid())
      sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    else
      sprintf(timeStr, "NoData");
    drawField(x1, col1_w, y0 + (field_h + field_gap), "Time", timeStr);

    if (gps.satellites.isValid()) sprintf(sats, "%d", gps.satellites.value());
    else                          sprintf(sats, "NoData");
    drawField(x1, col1_w, y0 + 2 * (field_h + field_gap), "Sat", sats);

    if (gps.speed.isValid()) sprintf(spd, "%.1f", gps.speed.kmph());
    else                     sprintf(spd, "NoData");
    drawField(x1, col1_w, y0 + 3 * (field_h + field_gap), "Spd", spd);

    sprintf(hdopStr, "%.1f", hdop);
    drawField(x1, col1_w, y0 + 4 * (field_h + field_gap), "HDOP", hdopStr);
  }

  delay(10);
}
