#pragma once
// Gas price data with no hardware in it (so it can be tested on a PC): a streaming reader for the AAA
// national average table. It does not depend on the page's markup: it drops the tags, and looks for the
// row labels ("Current Avg.", "Yesterday Avg." ...) and the dollar amounts that follow them.

#include <stdint.h>
#include <stddef.h>

enum GasWhen { GAS_NOW = 0, GAS_YESTERDAY, GAS_WEEK, GAS_MONTH, GAS_YEAR, GAS_WHEN_COUNT };
enum GasGrade { GAS_REGULAR = 0, GAS_MID, GAS_PREMIUM, GAS_DIESEL, GAS_GRADE_COUNT };

class AaaParser {
 public:
  void  begin();
  void  feed(char c);
  bool  done() const;                                    // every row has been read
  bool  has(int when, int grade) const { return when >= 0 && when < GAS_WHEN_COUNT && grade >= 0 && grade < GAS_GRADE_COUNT && grade < _np[when]; }
  float price(int when, int grade) const { return _v[when][grade]; }   // dollars per gallon
 private:
  static const int WIN = 20;
  char  _win[WIN + 1];
  int   _winLen = 0;
  bool  _inTag = false;
  int   _lab = -1;                                       // the row being read
  int   _gap = 0;                                        // characters since the last amount
  bool  _num = false;
  char  _nb[12];
  int   _nl = 0;
  float _v[GAS_WHEN_COUNT][GAS_GRADE_COUNT];
  int   _np[GAS_WHEN_COUNT];                             // amounts read for each row
  bool  _fin[GAS_WHEN_COUNT];                            // the row is complete
  void  text(char c);
  void  endNumber();
  void  finishRow();
};

// "+0.163" / "-0.012" / "0.000"
void gasFmtChange(char *out, size_t n, float delta);
