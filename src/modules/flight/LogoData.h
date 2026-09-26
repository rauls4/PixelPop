#pragma once
// Tables filled in by LogoData.cpp, which tools/make_logos.py generates from
// the folder of airline logo pictures. Do not edit by hand.

#include <stdint.h>

extern const uint16_t LOGO_COUNT;        // number of airline codes
extern const uint16_t LOGO_UNIQUE;       // number of different pictures
extern const uint8_t  LOGO_SIZE;         // pictures are LOGO_SIZE x LOGO_SIZE pixels
extern const uint16_t LOGO_CODES[];      // packed 3-character codes, sorted
extern const uint16_t LOGO_IMAGE[];      // picture number for each code
extern const uint32_t LOGO_OFFSET[];     // where each picture starts in LOGO_DATA (+1 end marker)
extern const uint8_t  LOGO_DATA[];       // run-length coded RGB565 pictures (LOGO_SIZE x LOGO_SIZE)
