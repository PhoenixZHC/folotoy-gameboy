// Deterministic, local-ROM differential benchmark; host time is not device FPS.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "gb_emulator.h"
extern "C" {
#include "minigb_apu.h"
}
static uint8_t sound_read(void *p, uint16_t a) { return minigb_apu_audio_read((minigb_apu_ctx *)p,a); }
static void sound_write(void *p,uint16_t a,uint8_t v) { minigb_apu_audio_write((minigb_apu_ctx *)p,a,v); }
static uint32_t hash_bytes(uint32_t hash,const void *data,size_t size) {
    const auto *bytes=static_cast<const uint8_t *>(data);
    for(size_t i=0;i<size;i++) hash=(hash^bytes[i])*16777619u;
    return hash;
}
int main(int argc,char **argv) {
    if(argc!=3) return 1;
    FILE *file=fopen(argv[1],"rb"); if(!file) return 2;
    fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    if(size<32768 || size>4194304) return 3;
    std::vector<uint8_t> rom(size);
    if(fread(rom.data(),1,size,file)!=(size_t)size) return 4;
    fclose(file);
    GBEmulator core;
    if(!core.loadRom("benchmark",rom.data(),rom.size()) || !core.init()) return 5;
    minigb_apu_ctx apu; minigb_apu_audio_init(&apu);
    core.soundContext_=&apu;core.soundRead_=sound_read;core.soundWrite_=sound_write;
    uint32_t video=2166136261u,audio=video,state=video;
    uint64_t draw_ns=0,skip_ns=0,audio_ns=0;
    unsigned frames=(unsigned)atoi(argv[2]); if(!frames) return 6;
    for(unsigned frame=0;frame<frames;frame++) {
        uint8_t buttons=0;
        if(frame>=300 && frame<1200 && frame%180<12) buttons|=INPUT_START;
        if(frame>=1200) {
            if(frame/180%4==0) buttons|=INPUT_RIGHT;
            if(frame/180%4==1) buttons|=INPUT_LEFT;
            if(frame/180%4==2) buttons|=INPUT_DOWN;
            if(frame%40<10) buttons|=INPUT_A;
            if(frame%67<12) buttons|=INPUT_B;
        }
        core.setInput(buttons);core.renderThisFrame_=frame%2==0;
        auto start=std::chrono::steady_clock::now();core.runFrame();
        uint64_t ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        (core.renderThisFrame_?draw_ns:skip_ns)+=ns;
        if(core.romIoFailed_) return 7;
        int16_t pcm[AUDIO_SAMPLES_TOTAL];start=std::chrono::steady_clock::now();
        minigb_apu_audio_callback(&apu,pcm);
        audio_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        audio=hash_bytes(audio,pcm,sizeof(pcm));video=hash_bytes(video,core.framebuf,5760);
        const uint16_t registers[]={core.pc_,core.sp_,core.a_,core.f_,core.b_,core.c_,core.d_,core.e_,core.h_,core.l_,core.ly_,core.iflag_,core.tima_};
        state=hash_bytes(state,registers,sizeof(registers));
        state=hash_bytes(state,core.wram,GBEmulator::GB_WRAM_SIZE);
        state=hash_bytes(state,core.vram,GBEmulator::GB_VRAM_SIZE);
        state=hash_bytes(state,core.hram,sizeof(core.hram));
        state=hash_bytes(state,core.oam,sizeof(core.oam));
        state=hash_bytes(state,core.sramData,core.sramPersistSize_);
    }
    printf("{\"frames\":%u,\"video\":%u,\"audio\":%u,\"state\":%u,\"draw_ns\":%llu,\"skip_ns\":%llu,\"audio_ns\":%llu}\n",frames,video,audio,state,(unsigned long long)draw_ns,(unsigned long long)skip_ns,(unsigned long long)audio_ns);
}
