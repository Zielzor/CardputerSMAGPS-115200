#include <M5Cardputer.h>    // Main library for M5Stack Cardputer
#include <TinyGPSPlus.h>    // Library for parsing GPS NMEA data
#include <SD.h>             // SD card library

#define GPS_RX 1            // GPS RX pin (Cardputer GPIO1)
#define GPS_TX 2            // GPS TX pin (Cardputer GPIO2)

HardwareSerial GPS_Serial(1);  // Use UART1 for GPS
TinyGPSPlus gps;               // TinyGPS++ object for parsing

File gpxFile;                  // File object for GPX logging
bool isRecording = false;      // GPX recording state
String filename;               // Name of the GPX file

// For parsing HDOP and Fix Quality from GGA NMEA sentence
String lastGGA;
float hdop = 0.0;
int fixQuality = 0;

// Display layout constants
const int screen_w = 240;      // Cardputer screen width
const int screen_h = 135;      // Cardputer screen height
const int gap = 4;             // Gap between columns and edges
const int total_w = screen_w - 3 * gap; // Total width for both columns
const int col0_w = total_w * 0.4;       // First column width (40%)
const int col1_w = total_w * 0.6;       // Second column width (60%)
const int field_h = 14;                 // Height of each data field
const int field_gap = 1;                // Gap between fields

// Parse GGA NMEA sentence for Fix Quality and HDOP
void parseGGA(const String& gga) {
  int field = 0;
  int lastIndex = 0;
  for (int i = 0; i < gga.length(); ++i) {
    if (gga[i] == ',' || gga[i] == '*') {
      String value = gga.substring(lastIndex, i);
      lastIndex = i + 1;
      field++;
      if (field == 6) fixQuality = value.toInt();   // 6th field: Fix Quality
      if (field == 9) hdop = value.toFloat();        // 9th field: HDOP
      if (field > 9) break;
    }
  }
}

// Draw a labeled data field (rectangle with label and value)
void drawField(int x, int w, int y, const char* label, const char* value) {
  M5Cardputer.Display.drawRect(x, y, w, field_h, TFT_WHITE); // Draw rectangle
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);    // Set text color
  M5Cardputer.Display.setCursor(x + 2, y + 3);               // Set cursor position
  M5Cardputer.Display.setTextSize(1);                        // Set text size
  M5Cardputer.Display.printf("%-5s: %s", label, value);      // Print label and value
}

// Create a new GPX file and write the header
void createGPX() {
  if (!isRecording) {
    filename = "/track_";
    if (gps.date.isValid()) {
      filename += String(gps.date.year()) + "-";
      filename += String(gps.date.month()) + "-";
      filename += String(gps.date.day()) + "_";
    }
    filename += String(millis() / 1000); // Add seconds to make filename unique
    filename += ".gpx";
    
    gpxFile = SD.open(filename, FILE_WRITE); // Open file for writing
    if (gpxFile) {
      gpxFile.println("<?xml version='1.0' encoding='UTF-8'?>");
      gpxFile.println("<gpx version='1.1'>");
      gpxFile.println("<trk><trkseg>");
      isRecording = true; // Set recording flag
    }
  }
}

// Write a single GPX trackpoint with all available data
void writeGPXPoint() {
  if (gps.location.isValid() && isRecording && gpxFile) {
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
    if (hdop > 0.0) {
      gpxFile.print("<hdop>");
      gpxFile.print(hdop, 1);
      gpxFile.println("</hdop>");
    }
    gpxFile.println("</trkpt>");
    gpxFile.flush(); // Ensure data is written to SD
  }
}

// Finish GPX file and close it
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
  M5Cardputer.begin(cfg); // Initialize Cardputer
  M5Cardputer.Display.setBrightness(32); // Set screen brightness (0-255, lower is dimmer)
  GPS_Serial.begin(115200, SERIAL_8N1, GPS_RX, GPS_TX); // Start GPS UART

  if (!SD.begin()) { // Initialize SD card
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("SD Card Fail!");
    while(1); // Halt if SD card not found
  }
}

void loop() {
  M5Cardputer.update(); // Update Cardputer state

  // Handle R key for GPX recording start/stop
  if (M5Cardputer.Keyboard.isKeyPressed('r')) {
    if (!isRecording) {
      createGPX(); // Start recording
    } else {
      stopRecording(); // Stop and save
    }
    delay(300); // Debounce delay
  }

  // Read GPS data and parse GGA for HDOP and Fix Quality
  while (GPS_Serial.available()) {
    char c = GPS_Serial.read();
    gps.encode(c); // Feed data to TinyGPS++
    static String nmeaLine = "";
    if (c == '\n') {
      if (nmeaLine.startsWith("$GPGGA")) {
        lastGGA = nmeaLine;
        parseGGA(lastGGA); // Parse GGA for HDOP and Fix Quality
      }
      nmeaLine = "";
    } else if (c != '\r') {
      nmeaLine += c;
    }
  }

  // Write GPX point every 3 seconds if recording
  static uint32_t lastWrite = 0;
  if (millis() - lastWrite > 3000 && isRecording) {
    writeGPXPoint();
    lastWrite = millis();
  }

  // --- DISPLAY UPDATE SECTION ---
  static uint32_t lastDisplay = 0;
  if (millis() - lastDisplay > 500) { // Update display every 500 ms
    lastDisplay = millis();

    M5Cardputer.Display.fillScreen(TFT_BLACK); // Clear screen

    // Draw header
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5Cardputer.Display.setCursor(gap, 2);
    M5Cardputer.Display.println("GPS Viewer");

    int y0 = 14;
    int x0 = gap;
    int x1 = gap * 2 + col0_w;

    // First column (5 fields)
    char lat[20], alt[20], date[20], qual[20], crs[20];
    if (gps.location.isValid())
      sprintf(lat, "%.6f", gps.location.lat());
    else
      sprintf(lat, "NoFix");
    drawField(x0, col0_w, y0, "Lat", lat);

    if (gps.altitude.isValid())
      sprintf(alt, "%.2f", gps.altitude.meters());
    else
      sprintf(alt, "NoData");
    drawField(x0, col0_w, y0 + (field_h + field_gap), "Alt", alt);

    if (gps.date.isValid())
      sprintf(date, "%02d/%02d/%02d", gps.date.day(), gps.date.month(), gps.date.year() % 100);
    else
      sprintf(date, "NoData");
    drawField(x0, col0_w, y0 + 2 * (field_h + field_gap), "Date", date);

    sprintf(qual, "%d", fixQuality);
    drawField(x0, col0_w, y0 + 3 * (field_h + field_gap), "Qual", qual);

    sprintf(crs, "%.1f", gps.course.deg());
    drawField(x0, col0_w, y0 + 4 * (field_h + field_gap), "Crs", crs);

    // Second column (5 fields)
    char lng[20], time[20], sats[20], spd[20], hdopStr[20];
    if (gps.location.isValid())
      sprintf(lng, "%.6f", gps.location.lng());
    else
      sprintf(lng, "NoFix");
    drawField(x1, col1_w, y0, "Lng", lng);

    if (gps.time.isValid())
      sprintf(time, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    else
      sprintf(time, "NoData");
    drawField(x1, col1_w, y0 + (field_h + field_gap), "Time", time);

    if (gps.satellites.isValid())
      sprintf(sats, "%d", gps.satellites.value());
    else
      sprintf(sats, "NoData");
    drawField(x1, col1_w, y0 + 2 * (field_h + field_gap), "Sat", sats);

    sprintf(spd, "%.1f", gps.speed.kmph());
    drawField(x1, col1_w, y0 + 3 * (field_h + field_gap), "Spd", spd);

    sprintf(hdopStr, "%.1f", hdop);
    drawField(x1, col1_w, y0 + 4 * (field_h + field_gap), "HDOP", hdopStr);

    // Show recording indicator if active
    if (isRecording) {
      M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
      M5Cardputer.Display.setCursor(screen_w - 60, 2);
      M5Cardputer.Display.print("[REC]");
    }
  }

  delay(10); // Small delay to avoid busy loop
}
