#pragma once

#include "app.h"

void ui_init(void);
void ui_shutdown(void);
void ui_draw(void);
// Draws (and blocks on) a simple progress frame while the library is scanned.
void ui_scan_progress(int files, const char *name);
void ui_message(const char *fmt, ...);
