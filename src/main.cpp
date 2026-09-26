// PixelPop: weather, forecast, air quality, clock, countdown, calendar, reminders, fireworks, Chomper, marquee, news ticker, photos, flight tracker, 
// chimes (Westminster or cuckoo) and an urgent notice with siren for a 64x32 HUB75 panel.
// Board: Waveshare ESP32-S3-RGB-Matrix (34422) + RGB-Matrix-P2.5-64x32 (23707).
//
// HOW IT IS ORGANIZED
//   src/core/      shared plumbing: panel, Wi-Fi + setup network, settings
//                  storage, time, the web site, and the page rotation
//   src/modules/   one folder per function (weather, forecast, air quality, clock, countdown, calendar, reminders, fireworks,
//                  chomper, flights, chime). Each has its own drawing code AND its own settings page.
//
// ADDING A FUNCTION
//   1. Make a folder under src/modules/ with a class derived from Module
//      (copy the countdown folder, it is the smallest example).
//   2. Create one instance below and pass it to app.add().
//
// FIRST-TIME WI-FI SETUP
//   With no saved Wi-Fi (or if it cannot connect) the board opens its own network
//   "PixelPop". Join it with your phone; a setup page opens (or browse to
//   192.168.4.1) where you pick your network and enter the password.
//
// SETTINGS SITE
//   After connecting, the panel shows its IP address for 5 seconds. Open that
//   address (or http://pixelpop.local) in a browser:
//     Home        an on/off toggle for every function, brightness, time zone
//     each function has its own page: Weather, Forecast, Air Quality, Clock, Countdown, Calendar, Reminders, Fireworks, Chomper, Marquee, News Ticker, Photos, Flights, Chimes
//   BOOT button: tap = show the IP address again, hold 5 s = change Wi-Fi.
//
// Libraries to install:
//   - "ESP32 HUB75 LED MATRIX PANEL DMA Display" and "Adafruit GFX Library"
//     (Library Manager)
// Everything else is part of the ESP32 core (3.x).
//
// Arduino board settings: "ESP32S3 Dev Module", USB CDC On Boot = Enabled.
//   PSRAM = "OPI PSRAM" (the board has 16 MB; the modules are kept there to spare the fast internal memory).
//   Partition layout: see partitions.csv next to this file.

#include <Arduino.h>
#include <new>
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif
#include "core/App.h"
#include "modules/weather/Weather.h"
#include "modules/forecast/Forecast.h"
#include "modules/air/Air.h"
#include "modules/clock/ClockModule.h"
#include "modules/countdown/Countdown.h"
#include "modules/calendar/Calendar.h"
#include "modules/reminders/Reminders.h"
#include "modules/fireworks/Fireworks.h"
#include "modules/chomper/Chomper.h"
#include "modules/marquee/Marquee.h"
#include "modules/ticker/Ticker.h"
#include "modules/seasonal/Seasonal.h"
#include "modules/gas/Gas.h"
#include "modules/menu/Menu.h"
#include "modules/plex/Plex.h"
#include "modules/photos/Photos.h"
#include "modules/flight/Flight.h"
#include "modules/chime/Chime.h"
#include "modules/urgent/Urgent.h"

// The module objects are kept in the big external memory (PSRAM) when there is one: as plain global variables they
// would sit in the board's small internal memory (about 30 KB together), which the secure connections
// (weather, news, ...) need. Each is created once in setup().
// (A macro rather than a template: the Arduino IDE's automatic function declarations cannot handle templates.)
static void *placeRaw(size_t n) {
  void *p = nullptr;
#if __has_include(<esp_heap_caps.h>)
  p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if (!p) p = malloc(n);
  if (p) memset(p, 0, n);                  // like a global variable: everything starts out zero
  return p;
}
#define PLACE(T) (new (placeRaw(sizeof(T))) T())

static WeatherModule  *weatherModule = nullptr;
static ForecastModule *forecastModule = nullptr;
static AirModule      *airModule = nullptr;
static ClockModule    *clockModule = nullptr;
static CountdownModule*countdownModule = nullptr;
static CalendarModule *calendarModule = nullptr;
static RemindersModule*remindersModule = nullptr;
static FireworksModule*fireworksModule = nullptr;
static ChomperModule  *chomperModule = nullptr;
static MarqueeModule  *marqueeModule = nullptr;
static TickerModule   *tickerModule = nullptr;
static SeasonalModule *seasonalModule = nullptr;
static GasModule      *gasModule = nullptr;
static MenuModule     *menuModule = nullptr;
static PlexModule     *plexModule = nullptr;
static PhotoModule    *photoModule = nullptr;
static FlightModule   *flightModule = nullptr;
static ChimeModule    *chimeModule = nullptr;
static UrgentModule   *urgentModule = nullptr;

void setup() {
  weatherModule = PLACE(WeatherModule);
  forecastModule = PLACE(ForecastModule);
  airModule = PLACE(AirModule);
  clockModule = PLACE(ClockModule);
  countdownModule = PLACE(CountdownModule);
  calendarModule = PLACE(CalendarModule);
  remindersModule = PLACE(RemindersModule);
  fireworksModule = PLACE(FireworksModule);
  chomperModule = PLACE(ChomperModule);
  marqueeModule = PLACE(MarqueeModule);
  tickerModule = PLACE(TickerModule);
  seasonalModule = PLACE(SeasonalModule);
  gasModule = PLACE(GasModule);
  menuModule = PLACE(MenuModule);
  plexModule = PLACE(PlexModule);
  photoModule = PLACE(PhotoModule);
  flightModule = PLACE(FlightModule);
  chimeModule = PLACE(ChimeModule);
  urgentModule = PLACE(UrgentModule);

  // The order here is the order of the pages on the panel and of the site's menu.
  app.add(weatherModule);
  app.add(forecastModule);
  app.add(airModule);
  app.add(clockModule);
  app.add(countdownModule);
  app.add(calendarModule);
  app.add(remindersModule);
  app.add(fireworksModule);
  app.add(chomperModule);
  app.add(marqueeModule);
  app.add(tickerModule);
  app.add(seasonalModule);
  app.add(gasModule);
  app.add(menuModule);
  app.add(plexModule);
  app.add(photoModule);
  app.add(flightModule);
  app.add(chimeModule);
  app.add(urgentModule);
  app.begin();
}

void loop() {
  app.loop();
}
