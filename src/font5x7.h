#pragma once

// 5x7 dot-matrix font shared by every display driver. Columns are bit0=top
// pixel..bit6=bottom pixel. Covers just what the status screens need: space,
// '-', '.', ':', digits, uppercase letters. Returns NULL for anything else.
const unsigned char *font5x7_glyph(char c);
