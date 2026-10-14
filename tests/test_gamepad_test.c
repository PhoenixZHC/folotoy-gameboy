#include <assert.h>
#include <limits.h>
#include <string.h>
#include "gamepad_test.h"
#include "gb_pixels.h"

static uint16_t pixels[240*320];
static unsigned texts;
static bool remote_label, remote_help, shoulder_label;
static void fill(void *context, int x,int y,int w,int h,uint16_t color) {
    (void)context;
    assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=240 && y+h<=320);
    for(int yy=y;yy<y+h;++yy) for(int xx=x;xx<x+w;++xx) pixels[yy*240+xx]=color;
}
static void text(void *context,int x,int y,const char *label,uint16_t color,bool small) {
    (void)context; (void)color;
    assert(label && x>=0 && y>=0 && y+(small?7:16)<=320);
    assert(x<240);
    assert(!strstr(label,"SYS") && !strstr(label,"CAP"));
    assert(strcmp(label,"L3") && strcmp(label,"R3"));
    if (!strcmp(label,"Select") || !strcmp(label,"Start")) assert(y>=273);
    ++texts;
    remote_label |= !strcmp(label,"MI RC / HORIZONTAL");
    remote_help |= !strcmp(label,"TV = A / MENU = B");
    shoulder_label |= !strcmp(label,"L") || !strcmp(label,"R") || !strcmp(label,"ZL") || !strcmp(label,"ZR");
}
int main(void) {
    gamepad_test_state_t state;
    gamepad_test_reset(&state,true,false);
    const int32_t axes[]={-512,511,0,128}, triggers[]={0,1023}, zero[4]={0};
    gamepad_test_update(&state,1,5,axes,triggers,0);
    assert(state.buttons==1 && state.dpad==5 && state.reports==1);
    assert(state.axes[0]==-512 && state.axes[1]==511 && state.triggers[1]==1023);
    assert(gamepad_test_recent(&state,0,0)); // Millisecond zero is a valid press.
    gamepad_test_update(&state,0,0,zero,zero,20);
    assert(!state.buttons && gamepad_test_recent(&state,0,599));
    assert(!gamepad_test_recent(&state,0,600));
    gamepad_test_update(&state,2,0,axes,triggers,UINT32_MAX-20);
    assert(gamepad_test_recent(&state,1,10)); // Timer wrap.
    assert(!gamepad_test_recent(&state,18,10));
    assert(gamepad_test_axis(INT32_MIN,22)==-22);
    assert(gamepad_test_axis(INT32_MAX,22)==22);
    assert(gamepad_test_axis(0,22)==0 && gamepad_test_axis(160,22)>0); // No game dead zone.
    gamepad_test_canvas_t canvas={NULL,fill,text};
    state.iine=true; state.firmware=0x2802;
    gamepad_test_state_t saved=state;
    gamepad_test_render(&state,10,120,&canvas);
    assert(!memcmp(&saved,&state,sizeof(state)) && texts>10);
    assert(pixels[128*240+42]==GB_LCD_DARK); // Left stick at left/down endpoint.
    assert(pixels[185*240+225]==GB_LCD_DARK); // Trigger border is bounded.
    gamepad_test_update(&state,1u<<8,0,zero,zero,100);
    gamepad_test_render(&state,100,50,&canvas);
    assert(pixels[95*240+54]==GB_LCD_DARK);
    assert(pixels[95*240+166]==GB_LCD_MID);
    gamepad_test_update(&state,1u<<9,0,zero,zero,101);
    gamepad_test_render(&state,101,50,&canvas);
    assert(pixels[95*240+54]==GB_LCD_MID); // 松开立即恢复，不能沿用 600 ms 高亮。
    assert(pixels[95*240+166]==GB_LCD_DARK);
    gamepad_test_update(&state,0,0,zero,zero,102);
    gamepad_test_render(&state,102,50,&canvas);
    assert(pixels[95*240+166]==GB_LCD_MID);
    assert(pixels[210*240+51]==GB_LCD_DARK); // 上箭头尖端。
    assert(pixels[210*240+47]==GB_LCD_MID); // 尖端两侧为空。
    // 所有按钮和方向松开后的第一帧都直接复原，不能经过浅色延时。
    const int xs[]={199,175,175,151,13,68,123,178,13,125};
    const int ys[]={228,250,206,228,155,155,155,155,274,274};
    gamepad_test_update(&state,0x3fff,15,zero,zero,2000);
    gamepad_test_render(&state,2000,50,&canvas);
    for(unsigned i=0;i<10;i++) assert(pixels[ys[i]*240+xs[i]]==GB_LCD_DARK);
    assert(pixels[206*240+41]==GB_LCD_DARK);
    gamepad_test_update(&state,0,0,zero,zero,2001);
    gamepad_test_render(&state,2001,50,&canvas);
    for(unsigned i=0;i<10;i++) assert(pixels[ys[i]*240+xs[i]]==GB_LCD_MID);
    assert(pixels[206*240+41]==GB_LCD_MID);
    gamepad_test_reset(&state,false,false);
    assert(!state.buttons && !state.reports && !state.seen_press);
    gamepad_test_update(&state,1,1,axes,triggers,50);
    assert(!state.buttons && !state.reports);
    gamepad_test_render(&state,50,-1,&canvas);
    gamepad_test_reset(&state,true,true);
    gamepad_test_update(&state,1,1,axes,triggers,60);
    assert(state.reports==1 && state.buttons==1 && state.dpad==1); // 键盘复用同一布局。
    gamepad_test_render(&state,60,50,&canvas);
    gamepad_test_reset(&state,true,false);
    gamepad_test_update(&state,3 | (1u<<10) | (1u<<11),1,zero,zero,70);
    remote_label=remote_help=shoulder_label=false;
    gamepad_test_render(&state,70,50,&canvas);
    assert(!remote_label && !remote_help && shoulder_label); // 所有设备固定显示原布局。
    assert(pixels[228*240+199]==GB_LCD_DARK && pixels[250*240+175]==GB_LCD_DARK);
    assert(pixels[274*240+13]==GB_LCD_DARK && pixels[274*240+125]==GB_LCD_DARK);
    gamepad_test_update(&state,0,0,zero,zero,71);
    gamepad_test_render(&state,71,50,&canvas);
    assert(pixels[228*240+199]==GB_LCD_MID && pixels[250*240+175]==GB_LCD_MID);
    gamepad_test_reset(&state,false,false);
    assert(!state.buttons && !state.reports);
    return 0;
}
