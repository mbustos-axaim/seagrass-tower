/*

    Copyright (C) 2016 Mauricio Bustos (m@bustos.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef poofer_h
#define poofer_h

#define POOFER_COUNT (2)
#define POOFER_1_PIN (31)
#define POOFER_2_PIN (26)

#define PATTERN_COUNT (4)
#define PATTERN_1_COUNT (18)
#define PATTERN_2_COUNT (26)
#define PATTERN_3_COUNT (18)
#define PATTERN_4_COUNT (24)

#define PATTERNS_OFF (-1)

typedef struct {
  unsigned long stepStart;
  int pooferState[POOFER_COUNT];
} patternStep;

typedef struct {
  int numberOfSteps;
  patternStep* steps;
} pattern;
  
class poofer {

 public:

  poofer();
  ~poofer();

  void startPattern(int id);
  void iteratePattern();

  void poof(int id, int state);
  
 private:
  
  int runningPattern;
  long patternStartTime;
  int stepNumber;
  
  int pooferPins[POOFER_COUNT];
  pattern* patterns;

};

#endif
