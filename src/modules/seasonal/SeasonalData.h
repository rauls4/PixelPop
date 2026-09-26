#pragma once
// The data of the In Season module: the small pictures and the list of what is in season where.

#include <stdint.h>

// ---- pictures ----
// Every picture is 9 x 9 pixels, drawn from characters:  . = nothing, 1..5 = one of the item's five colors.
// By habit: 1 = the main color, 2 = its shade, 3 = leaf green, 4 = highlight or detail, 5 = stem.
enum SpriteId : uint8_t {
  SP_ROUND, SP_PEAR, SP_CHERRY, SP_STRAWBERRY, SP_BERRY, SP_GRAPES, SP_WATERMELON, SP_MELON, SP_CITRUS, SP_AVOCADO,
  SP_CARROT, SP_LEAFY, SP_CABBAGE, SP_BROCCOLI, SP_BEET, SP_POTATO, SP_ONION, SP_PEAPOD, SP_CORN, SP_CUKE,
  SP_EGGPLANT, SP_PEPPER, SP_CHILI, SP_PUMPKIN, SP_MUSHROOM, SP_ASPARAGUS, SP_ARTICHOKE, SP_STALKS,
  SPRITE_COUNT
};
#define SPRITE_SIZE 9
extern const char *const SPRITES[SPRITE_COUNT][SPRITE_SIZE];

// ---- what is in season ----
enum Region : uint8_t {
  RG_MIDWEST, RG_NORTHEAST, RG_SOUTHEAST, RG_SOUTHCENTRAL, RG_MOUNTAIN, RG_PACIFICNW, RG_CALIFORNIA,
  RG_UK, RG_MEDITERRANEAN, RG_SOUTHERN,
  REGION_COUNT
};
extern const char *const REGION_NAMES[REGION_COUNT];

struct Produce {
  const char *name;
  uint8_t     veg;          // 0 fruit, 1 vegetable
  uint8_t     sprite;       // SpriteId
  uint32_t    color[5];     // 0xRRGGBB for the digits 1..5 in the picture
  const char *season;       // the months (1 = January) for each region, in the order of Region, separated by |.
                            // "8-11" a range, "5-6,9-10" several, "11-2" wraps over New Year, "" not in season.
                            // The last region (Southern Hemisphere) is not listed: it is the Mediterranean shifted by six months.
};
extern const Produce PRODUCE[];
extern const int     PRODUCE_COUNT;

// True if this item is in season in this region in this month (1..12).
bool produceInSeason(const Produce &p, int region, int month);
// Which region a place on the globe belongs to.
int  regionForLocation(float lat, float lon);
