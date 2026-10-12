// Live-control layout adapted from 1ikeMcFlurry/esp32c3-iine-gamepad.
// Copyright (c) 2026 1ikeMcFlurry. MIT; see third_party_licenses/iine-gamepad-MIT.txt.
#include "gamepad_test.h"
#include "gb_pixels.h"
#include <stdio.h>
#include <string.h>

void gamepad_test_reset(gamepad_test_state_t *s, bool connected, bool keyboard) {
    memset(s, 0, sizeof(*s));
    s->connected = connected;
    s->keyboard = keyboard;
}

void gamepad_test_update(gamepad_test_state_t *s, uint16_t buttons, uint8_t dpad,
                         const int32_t axes[4], const int32_t triggers[2], uint32_t now) {
    if (!s->connected || s->keyboard) return;
    uint32_t previous = s->buttons | ((uint32_t)s->dpad << 14);
    uint32_t current = (buttons & 0x3fff) | ((uint32_t)(dpad & 15) << 14);
    uint32_t rising = current & ~previous;
    for (unsigned bit = 0; bit < 18; ++bit)
        if (rising & (1u << bit)) s->pressed_at[bit] = now;
    s->seen_press |= rising;
    s->buttons = buttons & 0x3fff;
    s->dpad = dpad & 15;
    memcpy(s->axes, axes, sizeof(s->axes));
    memcpy(s->triggers, triggers, sizeof(s->triggers));
    s->reports++;
}

int gamepad_test_axis(int32_t value, int radius) {
    if (value < -512) value = -512;
    if (value > 511) value = 511;
    return value < 0 ? value * radius / 512 : value * radius / 511;
}

bool gamepad_test_recent(const gamepad_test_state_t *s, unsigned bit, uint32_t now) {
    return bit < 18 && (s->seen_press & (1u << bit)) &&
           (uint32_t)(now - s->pressed_at[bit]) < 600;
}

static void fill(const gamepad_test_canvas_t *c, int x, int y, int w, int h, uint16_t color) {
    c->fill(c->context, x, y, w, h, color);
}
static void text(const gamepad_test_canvas_t *c, int x, int y, const char *s, uint16_t color, bool small) {
    c->text(c->context, x, y, s, color, small);
}
static void frame(const gamepad_test_canvas_t *c, int x, int y, int w, int h, uint16_t color) {
    fill(c,x,y,w,1,color); fill(c,x,y+h-1,w,1,color);
    fill(c,x,y,1,h,color); fill(c,x+w-1,y,1,h,color);
}
static void circle(const gamepad_test_canvas_t *c, int x, int y, int r, uint16_t color) {
    for (int dy=-r; dy<=r; ++dy) {
        int dx=r;
        while (dx*dx+dy*dy > r*r) --dx;
        fill(c,x-dx,y+dy,2*dx+1,1,color);
    }
}
static void button(const gamepad_test_canvas_t *c, const gamepad_test_state_t *s,
                   uint32_t now, unsigned bit, const char *label, int x,int y,int w,int h) {
    (void)now;
    uint32_t mask = s->buttons | ((uint32_t)s->dpad << 14);
    bool down = (mask & (1u << bit)) != 0;
    uint16_t bg = down ? GB_LCD_DARK : GB_LCD_MID;
    fill(c,x,y,w,h,bg); frame(c,x,y,w,h,GB_LCD_DARK);
    text(c,x+(w-(int)strlen(label)*6)/2,y+(h-7)/2,label,
         down ? GB_LCD_LIGHT : GB_LCD_DARK,true);
}

// 直接绘制箭头，避免依赖字体或把方向误读成肩键 L/R。
static void direction(const gamepad_test_canvas_t *c, const gamepad_test_state_t *s,
                      uint32_t now, unsigned index, int x, int y) {
    (void)now;
    bool down = (s->dpad & (1u << index)) != 0;
    fill(c,x,y,22,20,down ? GB_LCD_DARK : GB_LCD_MID);
    frame(c,x,y,22,20,GB_LCD_DARK);
    uint16_t color = down ? GB_LCD_LIGHT : GB_LCD_DARK;
    for (int v = -5; v <= 5; ++v) for (int u = -5; u <= 5; ++u) {
        bool on = (v <= 0 && u >= -v-5 && u <= v+5) || (v > 0 && u >= -1 && u <= 1);
        if (!on) continue;
        int dx = u, dy = v;
        if (index == 1) dy = -v;
        else if (index == 2) { dx = -v; dy = u; }
        else if (index == 3) { dx = v; dy = u; }
        fill(c,x+11+dx,y+10+dy,1,1,color);
    }
}

void gamepad_test_render(const gamepad_test_state_t *s, uint32_t now, int battery,
                          const gamepad_test_canvas_t *c) {
    char line[48];
    fill(c,0,0,240,320,GB_LCD_LIGHT);
    frame(c,2,2,236,286,GB_LCD_DARK);
    fill(c,6,41,228,3,GB_LCD_DARK);
    fill(c,9,8,4,23,GB_LCD_SHADE);
    text(c,17,13,"手柄测试",GB_LCD_DARK,false);
    frame(c,199,8,29,14,GB_LCD_DARK);
    fill(c,228,12,3,6,GB_LCD_DARK);
    if (battery >= 0) {
        int percent=battery>100?100:battery;
        fill(c,201,10,percent*25/100,10,GB_LCD_DARK);
        snprintf(line,sizeof(line),"%d%%",percent);
    } else snprintf(line,sizeof(line),"?");
    text(c,201,27,line,GB_LCD_DARK,true);
    if (!s->connected || s->keyboard) {
        text(c,20,104,s->keyboard?"当前连接为键盘":"手柄未连接",GB_LCD_DARK,false);
        text(c,20,140,"请返回主菜单配对",GB_LCD_DARK,false);
    } else {
        if (s->iine) snprintf(line,sizeof(line),"L1241 %02X.%02X %s",s->firmware>>8,s->firmware&255,
                             s->firmware==0x2702?"VERIFIED":"UNVERIFIED");
        else snprintf(line,sizeof(line),"BLE HID");
        text(c,12,51,line,GB_LCD_DARK,true);
        snprintf(line,sizeof(line),"REPORTS %lu",(unsigned long)s->reports);
        text(c,12,63,line,GB_LCD_DARK,true);
        for (unsigned side=0;side<2;++side) {
            int x=side?176:64; unsigned a=side*2;
            bool pressed = (s->buttons & (1u << (8+side))) != 0;
            circle(c,x,106,29,GB_LCD_DARK);
            circle(c,x,106,28,pressed ? GB_LCD_DARK : GB_LCD_MID);
            fill(c,x-24,106,49,1,GB_LCD_SHADE); fill(c,x,82,1,49,GB_LCD_SHADE);
            circle(c,x+gamepad_test_axis(s->axes[a],22),106+gamepad_test_axis(s->axes[a+1],22),4,
                   pressed ? GB_LCD_LIGHT : GB_LCD_DARK);
            snprintf(line,sizeof(line),"%s %ld/%ld",side?"R":"L",(long)s->axes[a],(long)s->axes[a+1]);
            text(c,side?127:15,140,line,GB_LCD_DARK,true);
        }
        const char *shoulders[]={"L","R","ZL","ZR"};
        for(unsigned i=0;i<4;++i) button(c,s,now,4+i,shoulders[i],12+i*55,154,51,19);
        for(unsigned i=0;i<2;++i) {
            int32_t v=s->triggers[i]; if(v<0)v=0; if(v>1023)v=1023;
            int x=i?128:12;
            frame(c,x,179,100,7,GB_LCD_DARK);
            fill(c,x+1,180,v*98/1023,5,GB_LCD_SHADE);
            snprintf(line,sizeof(line),"%s %ld",i?"ZR":"ZL",(long)s->triggers[i]);
            text(c,x,190,line,GB_LCD_DARK,true);
        }
        const int px[]={40,40,64,16},py[]={205,249,227,227};
        for(unsigned i=0;i<4;++i) direction(c,s,now,i,px[i],py[i]);
        const int bx[]={198,174,174,150},by[]={227,249,205,227};
        const char *face[]={"A","B","X","Y"};
        for(unsigned i=0;i<4;++i) button(c,s,now,i,face[i],bx[i],by[i],22,20);
        button(c,s,now,10,"Select",12,273,104,12);
        button(c,s,now,11,"Start",124,273,104,12);
    }
    fill(c,0,289,240,31,GB_LCD_DARK);
    text(c,12,293,"长按OK/Select返回",GB_LCD_LIGHT,false);
    text(c,12,312,"INPUT ONLY / NO RUMBLE TEST",GB_LCD_LIGHT,true);
}
