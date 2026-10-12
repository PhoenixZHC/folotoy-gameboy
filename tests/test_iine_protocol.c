// SPDX-License-Identifier: MIT
// Copyright (c) 2026 1ikeMcFlurry
// Adapted from https://github.com/1ikeMcFlurry/esp32c3-iine-gamepad
// See third_party_licenses/iine-gamepad-MIT.txt.
#include "parser/uni_iine_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void test_input(void)
{
    iine_input_state_t state={.hat=-1};
    const uint8_t center[]={12,0,18,128,128,128,129,0,19,128,128,129,130};
    assert(iine_input_decode(center,sizeof(center),&state));
    assert(state.axes_present==0x1b && state.axes[0]==128 && state.axes[4]==128);
    const uint8_t a[]={2,1,1},b[]={2,1,2},release_a[]={2,0,1},release_ab[]={4,0,1,0,2};
    assert(iine_input_decode(a,sizeof(a),&state) && state.buttons==1);
    assert(iine_input_decode(b,sizeof(b),&state) && state.buttons==3);
    assert(iine_input_decode(release_a,sizeof(release_a),&state) && state.buttons==2);
    assert(iine_input_decode(release_ab,sizeof(release_ab),&state) && !state.buttons);
    const uint8_t stick_and_l3[]={8,1,11,1,18,0,255,0,255};
    assert(iine_input_decode(stick_and_l3,sizeof(stick_and_l3),&state));
    assert(state.buttons==(1u<<10) && state.axes[0]==0 && state.axes[1]==255 && state.axes[3]==128);
    const uint8_t trigger[]={4,1,7,255,0},trigger_off[]={4,0,7,0,0};
    assert(iine_input_decode(trigger,sizeof(trigger),&state) && state.axes[2]==255 && (state.buttons&(1u<<6)));
    assert(iine_input_decode(trigger_off,sizeof(trigger_off),&state) && state.axes[2]==0 && !(state.buttons&(1u<<6)));
    const uint8_t diagonal[]={4,1,14,1,15},up_off[]={2,0,14};
    assert(iine_input_decode(diagonal,sizeof(diagonal),&state) && state.dpad==5);
    assert(iine_input_decode(up_off,sizeof(up_off),&state) && state.dpad==4);
    iine_input_state_t saved=state;
    const uint8_t truncated[]={5,1,1,1,18,255},unknown[]={4,1,1,1,0xfe},bad_state[]={2,2,1};
    assert(!iine_input_decode(truncated,sizeof(truncated),&state));
    assert(!memcmp(&saved,&state,sizeof(state)));
    assert(!iine_input_decode(unknown,sizeof(unknown),&state));
    assert(!memcmp(&saved,&state,sizeof(state)));
    assert(!iine_input_decode(bad_state,sizeof(bad_state),&state));
    for(size_t n=0;n<sizeof(center);++n) assert(!iine_input_decode(center,n,&state));
    assert(!memcmp(&saved,&state,sizeof(state)));
}

static void replay(const char *path)
{
    FILE *file=fopen(path,"r"); assert(file);
    char line[256];unsigned count=0;uint32_t buttons=0;uint8_t dpad=0;
    iine_input_state_t state={.hat=-1};
    int lo[6]={255,255,255,255,255,255},hi[6]={0};
    while(fgets(line,sizeof(line),file)) {
        if(line[0]=='#' || line[0]=='\n') continue;
        uint8_t payload[15];size_t length=0;char *cursor=line,*next;
        for(;;) {
            unsigned long value=strtoul(cursor,&next,16);
            if(next==cursor) break;
            assert(length<sizeof(payload) && value<=255);
            payload[length++]=value;cursor=next;
        }
        if(!length) continue;
        if(!iine_input_decode(payload,length,&state)) {fprintf(stderr,"Invalid captured packet %u: %s",count,line);abort();}
        ++count;buttons|=state.buttons;dpad|=state.dpad;
        for(unsigned i=0;i<6;++i) if(state.axes_present&(1u<<i)) {
            if(state.axes[i]<lo[i]) lo[i]=state.axes[i];
            if(state.axes[i]>hi[i]) hi[i]=state.axes[i];
        }
    }
    fclose(file);
    assert(count>100 && buttons==0xfff && dpad==15);
    for(unsigned i=0;i<6;++i) assert(lo[i]==0 && hi[i]==255);
    assert(state.buttons==0 && state.dpad==0);
    printf("L1241 replay: %u captured packets, 12 buttons/release, dpad, 6 axes [0,255] passed\n",count);
}

int main(int argc,char **argv)
{
    /* Captured X20 exchange from the public protocol investigation, not a
     * self-generated vector. Proves the wire format against another client. */
    const uint8_t request[] = {0x9a,0x88,0xc2,0x7a,0x4b};
    const uint8_t response[] = {0x26,0x8c,0xb6,0x8e,0xf7,0x4c,0x7e,0x10,0xbf,0xc1,0xfd,0xdf,0x62};
    const uint8_t payload[] = {7,0x10,0x13,0x20,9,1,0x23,0x52};
    uint8_t wire[IINE_PACKET_MAX];
    assert(iine_packet_build(wire,sizeof(wire),0x91,1,0x43,NULL,0)==sizeof(request));
    assert(!memcmp(wire,request,sizeof(request)));
    iine_packet_t p;
    assert(iine_packet_parse(response,sizeof(response),&p));
    assert(p.opcode==0x19 && p.serial==1 && p.length==sizeof(payload));
    assert(!memcmp(p.payload,payload,sizeof(payload)));
    for(size_t n=0;n<sizeof(response);++n) {
        memcpy(wire,response,sizeof(response));wire[n]^=1;
        assert(!iine_packet_parse(wire,sizeof(response),&p));
    }
    for(size_t n=0;n<sizeof(response);++n) assert(!iine_packet_parse(response,n,&p));
    assert(!iine_packet_parse(NULL,0,&p));
    assert(!iine_packet_parse(wire,21,&p));
    assert(!iine_packet_build(wire,4,0x91,1,0,NULL,0));
    assert(!iine_packet_build(wire,sizeof(wire),0x91,1,0,NULL,1));
    assert(!iine_packet_build(wire,sizeof(wire),0x91,1,0,wire,16));
    /* Exercise both scrambling offsets, byte overflow and maximum payload. */
    uint8_t data[IINE_PAYLOAD_MAX];
    for(unsigned seed=0;seed<256;++seed) {
        for(size_t n=0;n<=IINE_PAYLOAD_MAX;++n) {
            for(size_t i=0;i<n;++i) data[i]=seed+i*31;
            size_t bytes=iine_packet_build(wire,sizeof(wire),0x1f,255-seed,seed,data,n);
            assert(bytes==n+5 && iine_packet_parse(wire,bytes,&p));
            assert(p.opcode==0x1f && p.serial==255-seed && p.length==n);
            assert(!memcmp(p.payload,data,n));
        }
    }
    const uint8_t mode[]={2,0xdf,0xab},test[]={5,0xdf,0xab,0,6,5};
    const uint8_t ota[]={5,0xdf,0xab,0,2,14},reset[]={3,0xdf,0xa9,1};
    assert(iine_request_allowed(0x91,NULL,0));
    assert(iine_request_allowed(0x1d,mode,sizeof(mode)));
    assert(iine_request_allowed(0x1e,mode,sizeof(mode)));
    assert(iine_request_allowed(0x1d,test,sizeof(test)));
    assert(!iine_request_allowed(0x1d,ota,sizeof(ota)));
    assert(!iine_request_allowed(0x16,reset,sizeof(reset)));
    for(unsigned op=0x31;op<=0x37;++op) assert(!iine_request_allowed(op,mode,sizeof(mode)));
    puts("IINE codec: captured exchange, corruption/truncation, bounds, request allowlist passed");
    test_input();
    if(argc==2) replay(argv[1]);
    return 0;
}
