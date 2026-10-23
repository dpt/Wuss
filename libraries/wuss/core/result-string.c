/* wuss/result-string.c -- map result_t codes to human-readable strings */

#include "wuss/wuss.h"

const char *wuss_result_string(result_t err)
{
  switch (err)
  {
  case result_WUSS_TOO_SMALL:   return "Wuss: dimensions too small";
  case result_WUSS_BAD_COLOUR:  return "Wuss: palette index out of range";
  case result_WUSS_BAD_ICON:    return "Wuss: malformed icon spec";
  case result_WUSS_BAD_PALETTE: return "Wuss: malformed palette file";

  default:                      return result_string(err);
  }
}
