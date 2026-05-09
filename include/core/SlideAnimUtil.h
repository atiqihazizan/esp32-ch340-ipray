#ifndef SLIDE_ANIM_UTIL_H
#define SLIDE_ANIM_UTIL_H

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <math.h>

extern Adafruit_SSD1306 display;

// ================================================================
// SLIDE ANIM UTIL
// ================================================================
// Utility untuk animasi "slide-in-up" (efek helai buku nota) bagi
// mana-mana field bernombor: jam, minit, saat, hari, bulan, tahun.
//
// Cara guna:
//   1. Cipta SlideField static untuk setiap field (sekali sahaja)
//   2. Panggil slideDrawField(...) dalam fungsi runOled* dengan
//      nilai semasa, posisi, saiz teks, dll.
//   3. Util akan auto-detect perubahan nilai → trigger animasi
//
// Contoh:
//   static SlideField fHour;
//   slideDrawField(fHour, now.hour(), 8, 8, 2, 2);  // jam, x=8, y=8, size=2, 2 digit
// ================================================================

// ── Konfigurasi default ──
#define SLIDE_DEFAULT_DURATION_MS  300

// Adafruit GFX glyph dimensions:
//   textSize 1: 5px wide × 7px tall (+1px spacing each side = 6×8 cell)
//   textSize 2: 10px wide × 14px tall (+2px spacing = 12×16 cell)
// Untuk clipping selamat, kita guna saiz cell PENUH + sedikit padding
#define SLIDE_GLYPH_W_SIZE1        6     // textSize 1: 6px lebar setiap aksara
#define SLIDE_GLYPH_H_SIZE1        8     // textSize 1: 8px tinggi
#define SLIDE_GLYPH_W_SIZE2        12    // textSize 2: 12px lebar
#define SLIDE_GLYPH_H_SIZE2        16    // textSize 2: 16px tinggi
#define SLIDE_CLIP_PAD_X           2     // padding x untuk clip (elak potong glyph)

// ================================================================
// STRUCT: State animasi setiap field
// ================================================================
struct SlideField {
  int      lastValue   = -1;     // nilai dari frame sebelumnya
  int      prevValue   = -1;     // nilai LAMA (untuk lukis semasa animasi)
  uint32_t animStartMs = 0;
  bool     animating   = false;
};

// ================================================================
// HELPER: Kira tinggi & lebar digit ikut textSize
// ================================================================
inline int slideGlyphH(int textSize) {
  return (textSize == 2) ? SLIDE_GLYPH_H_SIZE2 : SLIDE_GLYPH_H_SIZE1;
}
inline int slideGlyphW(int textSize) {
  return (textSize == 2) ? SLIDE_GLYPH_W_SIZE2 : SLIDE_GLYPH_W_SIZE1;
}

// ================================================================
// EASING: ease-out cubic
// ================================================================
inline float slideEaseOut(float t) {
  return 1.0f - powf(1.0f - t, 3.0f);
}

// ================================================================
// CORE: Lukis field dengan animasi slide-up
//
// PENTING: Clipping HANYA pada zon glyph itu sendiri (y..y+glyphH).
// Tak sentuh kawasan luar — pemanggil bertanggungjawab pastikan
// elemen lain (separator, tarikh, dll) dilukis SELEPAS field
// kalau berada di kawasan tindih.
//
// Cara kerja clipping:
//   1. Lukis fillRect HITAM saiz tepat pada zon glyph
//   2. Lukis nilai lama (di atas, mungkin terkeluar)
//   3. Lukis nilai baru (di bawah, mungkin terkeluar)
//   4. Lukis fillRect HITAM lagi atas + bawah zon untuk crop
//
// Parameter:
//   f         — state animasi (static SlideField di pemanggil)
//   value     — nilai semasa
//   x, y      — kedudukan akhir digit (top-left)
//   textSize  — 1 atau 2
//   digits    — bilangan digit
//   duration  — tempoh animasi (ms)
// ================================================================
void slideDrawField(SlideField &f, int value, int x, int y,
                    int textSize, int digits,
                    uint32_t duration = SLIDE_DEFAULT_DURATION_MS) {
  // ── Detect perubahan nilai → trigger animasi ──
  if (value != f.lastValue) {
    f.prevValue   = f.lastValue;
    f.lastValue   = value;
    f.animStartMs = millis();
    f.animating   = (f.prevValue != -1);
  }

  int glyphH = slideGlyphH(textSize);
  int glyphW = slideGlyphW(textSize);
  int zoneW  = glyphW * digits;

  // ── Kira offset semasa ──
  int offset = 0;
  if (f.animating) {
    uint32_t elapsed = millis() - f.animStartMs;
    if (elapsed >= duration) {
      f.animating = false;
      offset      = 0;
    } else {
      float t     = (float)elapsed / duration;
      float eased = slideEaseOut(t);
      offset      = (int)(glyphH * (1.0f - eased));
    }
  }

  // ── Format nombor ──
  char fmt[8];
  snprintf(fmt, sizeof(fmt), "%%0%dd", digits);

  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);

  // ── Padam zon SEKARANG sahaja (saiz tepat) ──
  // Ini bersihkan kawasan sebelum lukis digit baru
  display.fillRect(x, y, zoneW, glyphH, SSD1306_BLACK);

  // ── Lukis nilai LAMA (slide keluar atas) ──
  if (f.animating) {
    char oldBuf[8];
    snprintf(oldBuf, sizeof(oldBuf), fmt, f.prevValue);
    display.setCursor(x, y - (glyphH - offset));
    display.print(oldBuf);
  }

  // ── Lukis nilai BARU (slide masuk dari bawah) ──
  char newBuf[8];
  snprintf(newBuf, sizeof(newBuf), fmt, value);
  display.setCursor(x, y + offset);
  display.print(newBuf);

  // ── CROP: padam pixel yang terkeluar atas/bawah zon ──
  // Hanya padam dalam x..x+zoneW (tak sentuh field jiran)
  // Atas: 1 baris glyph penuh di atas zon
  display.fillRect(x, y - glyphH, zoneW, glyphH, SSD1306_BLACK);
  // Bawah: 1 baris glyph penuh di bawah zon
  display.fillRect(x, y + glyphH, zoneW, glyphH, SSD1306_BLACK);
}

// ================================================================
// HELPER: Lukis pemisah statik (e.g. ":" atau "/")
// Berguna untuk letak antara field yang dianimasi
// ================================================================
void slideDrawSeparator(const char *sep, int x, int y, int textSize) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(sep);
}

#endif