#pragma once
// In Season module: the fruits and vegetables that are in season this month where you live, each with a
// little picture, scrolling along the screen. The region comes from the location on the Weather page
// (or you pick one), the month from the clock. The lists are in SeasonalData.cpp.

#include "../../core/Module.h"
#include "SeasonalData.h"

class SeasonalModule : public Module {
 public:
  SeasonalModule() : Module("seasonal", "In Season", "s_", true, 30) {}
  const char *version() const override { return "1.4.2"; }      // bump when this module changes (see CHANGELOG.md)

  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                       // the items scroll
  void pageEntered(int sub) override { (void)sub; _fresh = true; }     // start a new showing (new pick, scroll from the right)
  int  pageProgress() override { return 0; }                 // the configured rotation duration controls when to advance

  const char *enabledLabel() override { return "Show what is in season in the rotation"; }
  String summary() override;
  String actionsHtml() override;
  void   registerRoutes(WebServer &server) override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  uint8_t  _region = 0;                 // 0 = automatic (from the Weather page), else Region + 1
  uint8_t  _month = 0;                  // 0 = this month, else 1..12
  bool     _fruits = true, _veg = true;
  uint8_t  _speed = 1;                  // 0 slow, 1 normal, 2 fast
  uint8_t  _maxItems = 10;              // how many items one showing has (picked at random); 0 = all of them
  uint32_t _colTitle = 0x00C8FF, _colFruit = 0xFFD200, _colVeg = 0x60E070;

  // the list being shown (rebuilt when the region, month or choices change)
  static const int MAX_ITEMS = 64;
  int      _key = -1;                   // region / month / choices the list was built for
  int      _n = 0;
  uint8_t  _idx[MAX_ITEMS];             // index into PRODUCE
  uint8_t  _show[MAX_ITEMS];            // what the current showing has: indexes into PRODUCE, in the order shown
  int      _shownN = 0;
  bool     _needPick = true;            // choose the items for a showing at the next draw
  uint32_t _rng = 0x9E3779B9u;
  int      _start = 0;                  // with "all of them": where the next showing starts in the list
  int      _lap = 0;                    // width of one round of the list, in pixels
  int      _regionNow = 0, _monthNow = 1;

  // scrolling
  unsigned long _t0 = 0, _lastDraw = 0, _lastStep = 0;
  bool          _fresh = true;             // the next draw starts a new showing
  bool          _done = false;             // the last item has scrolled off the screen: start a new round

  int  activeRegion() const;
  int  activeMonth() const;
  void refreshList();
  void pickItems();
  uint32_t rnd();
  int  itemWidth(const Produce &p, int s) const;
  unsigned long msPerPx() const;
};
