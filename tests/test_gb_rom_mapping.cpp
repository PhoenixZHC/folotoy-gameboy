#include <assert.h>
#include <stdio.h>
#include <vector>
#include "gb_emulator.h"

static void check_cart(uint8_t type) {
    unsigned banks = type == 5 ? 16 : 128;
    std::vector<uint8_t> rom(banks * 16384);
    for (unsigned bank=0; bank<banks; bank++)
        memset(rom.data()+bank*16384, bank, 16384);
    rom[0x147]=type; rom[0x149]=0;
    GBEmulator core;
    assert(core.loadRom("test",rom.data(),rom.size()) && core.init());
    assert(core.readByte(0x345)==0 && core.readByte(0x4567)==1);
    core.writeByte(0x2100,5);
    assert(core.readByte(0x4567)==5);
    core.writeByte(0x2100,0);
    assert(core.readByte(0x4567)==(type==0x19 ? 0 : 1));
    if(type==1) {
        core.writeByte(0x4000,2);
        assert(core.readByte(0x4567)==65);
        core.writeByte(0x6000,1);
        assert(core.readByte(0x345)==64);
        core.writeByte(0x6000,0);
        assert(core.readByte(0x345)==0);
    }
    if(type==0x19) {
        core.writeByte(0x2000,0xff);
        core.writeByte(0x3000,1);
        assert(core.readByte(0x4567)==127);
    }
    core.reset();
    assert(core.readByte(0x345)==0 && core.readByte(0x4567)==1);
}
static void check_fetch_boundaries() {
    std::vector<uint8_t> rom(32768,0);
    rom[0x3ffe]=0x01; rom[0x3fff]=0x34; rom[0x4000]=0x12;
    GBEmulator core;
    assert(core.loadRom("test",rom.data(),rom.size()) && core.init());
    core.pc_=0x3ffe;
    core.cpu_step();
    assert(core.b_==0x12 && core.c_==0x34 && core.pc_==0x4001);
    core.writeByte(0xc000,0x3e); core.writeByte(0xc001,0x42);
    core.pc_=0xc000; core.cpu_step();
    assert(core.a_==0x42 && core.pc_==0xc002);
    core.ie_=0; core.pc_=0xffff; core.cpu_step();
    assert(core.pc_==0);
}
int main(){for(uint8_t type : {1,5,0x11,0x19}) check_cart(type); check_fetch_boundaries(); puts("ROM mapping: PASS");}
