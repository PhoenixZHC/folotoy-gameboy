#include "SdFat.h"

FsFile::FsFile(FsFile &&) noexcept {}
FsFile &FsFile::operator=(FsFile &&) noexcept { return *this; }
bool FsFile::open(const char *, int) { return false; }
bool FsFile::attachRom(RomReadFn, void *, uint32_t) { return false; }
bool FsFile::seek(uint32_t) { return false; }
int FsFile::read(void *, size_t) { return -1; }
size_t FsFile::write(const void *, size_t) { return 0; }
size_t FsFile::print(const char *) { return 0; }
size_t FsFile::print(char) { return 0; }
int FsFile::fgets(char *, int) { return -1; }
uint32_t FsFile::size() const { return 0; }
int FsFile::available() const { return 0; }
bool FsFile::sync() { return false; }
void FsFile::close() {}
FsFile SdFat::open(const char *, int) { return FsFile(); }
bool SdFat::exists(const char *) { return false; }
bool SdFat::remove(const char *) { return false; }
bool SdFat::rename(const char *, const char *) { return false; }
bool SdFat::mkdir(const char *) { return false; }
