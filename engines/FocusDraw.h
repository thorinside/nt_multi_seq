#ifndef FOCUS_DRAW_H
#define FOCUS_DRAW_H

#include <distingnt/api.h>

namespace FocusDraw {
    inline void text(int x, int y, const char* str, int color) {
        NT_drawText(x, y, str, color, kNT_textLeft, kNT_textTiny);
    }
    inline void rect(int x1, int y1, int x2, int y2, int color) {
        NT_drawShapeI(kNT_rectangle, x1, y1, x2, y2, color);
    }
    inline void line(int x1, int y1, int x2, int y2, int color) {
        NT_drawShapeI(kNT_line, x1, y1, x2, y2, color);
    }
    inline int intToStr(char* buf, int32_t v) {
        return NT_intToString(buf, v);
    }
    inline int floatToStr(char* buf, float v, int decimals) {
        return NT_floatToString(buf, v, decimals);
    }
}

#endif
