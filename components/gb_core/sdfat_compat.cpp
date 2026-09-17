#include "SdFat.h"

#include <string.h>
#include <sys/stat.h>

FsFile::FsFile(FsFile &&other) noexcept { *this = static_cast<FsFile &&>(other); }

FsFile &FsFile::operator=(FsFile &&other) noexcept {
    if (this == &other) return *this;
    close();
    file_ = other.file_;
    rom_read_ = other.rom_read_;
    rom_context_ = other.rom_context_;
    length_ = other.length_;
    position_ = other.position_;
    other.file_ = nullptr;
    other.rom_read_ = nullptr;
    return *this;
}

bool FsFile::open(const char *path, int flags) {
    close();
    const char *mode = (flags & O_TRUNC) ? "wb" : (flags & O_WRONLY) ? "ab" : "rb";
    file_ = fopen(path, mode);
    return file_ != nullptr;
}

bool FsFile::attachRom(RomReadFn read, void *context, uint32_t length) {
    close();
    if (!read || length == 0) return false;
    rom_read_ = read;
    rom_context_ = context;
    length_ = length;
    position_ = 0;
    return true;
}

bool FsFile::seek(uint32_t offset) {
    if (rom_read_) {
        if (offset > length_) return false;
        position_ = offset;
        return true;
    }
    return file_ && fseek(file_, (long)offset, SEEK_SET) == 0;
}

int FsFile::read(void *dst, size_t bytes) {
    if (!dst) return -1;
    if (rom_read_) {
        if (position_ > length_ || bytes > length_ - position_) return -1;
        if (!rom_read_(rom_context_, position_, dst, bytes)) return -1;
        position_ += bytes;
        return (int)bytes;
    }
    return file_ ? (int)fread(dst, 1, bytes, file_) : -1;
}

size_t FsFile::write(const void *src, size_t bytes) {
    return file_ ? fwrite(src, 1, bytes, file_) : 0;
}

size_t FsFile::print(const char *text) { return write(text, strlen(text)); }
size_t FsFile::print(char character) { return write(&character, 1); }
int FsFile::fgets(char *dst, int bytes) {
    if (!file_ || !::fgets(dst, bytes, file_)) return -1;
    return (int)strlen(dst);
}

uint32_t FsFile::size() const {
    if (rom_read_) return length_;
    if (!file_) return 0;
    long here = ftell(file_);
    if (here < 0 || fseek(file_, 0, SEEK_END) != 0) return 0;
    long end = ftell(file_);
    fseek(file_, here, SEEK_SET);
    return end < 0 ? 0 : (uint32_t)end;
}

int FsFile::available() const {
    if (rom_read_) return position_ <= length_ ? (int)(length_ - position_) : 0;
    if (!file_) return 0;
    long here = ftell(file_);
    uint32_t end = size();
    return here >= 0 && (uint32_t)here < end ? (int)(end - (uint32_t)here) : 0;
}

bool FsFile::sync() { return file_ && fflush(file_) == 0; }
void FsFile::close() {
    if (file_) fclose(file_);
    file_ = nullptr;
    rom_read_ = nullptr;
}

FsFile SdFat::open(const char *path, int flags) {
    FsFile result;
    result.open(path, flags);
    return result;
}
bool SdFat::exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
bool SdFat::remove(const char *path) { return ::remove(path) == 0; }
bool SdFat::rename(const char *a, const char *b) { return ::rename(a, b) == 0; }
bool SdFat::mkdir(const char *path) { return ::mkdir(path, 0777) == 0; }
