#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>

using RomReadFn = bool (*)(void *, uint32_t, void *, size_t);

// 仅覆盖 SUMI 核心编译所需接口；ROM 读写使用受边界约束的 Flash 分区视图。
class FsFile {
public:
    FsFile() = default;
    ~FsFile() { close(); }
    FsFile(const FsFile &) = delete;
    FsFile &operator=(const FsFile &) = delete;
    FsFile(FsFile &&other) noexcept;
    FsFile &operator=(FsFile &&other) noexcept;

    bool open(const char *path, int flags);
    bool attachRom(RomReadFn read, void *context, uint32_t length);
    bool seek(uint32_t offset);
    int read(void *dst, size_t bytes);
    size_t write(const void *src, size_t bytes);
    size_t print(const char *text);
    size_t print(char character);
    int fgets(char *dst, int bytes);
    uint32_t size() const;
    int available() const;
    bool sync();
    void close();
    explicit operator bool() const { return file_ != nullptr || rom_read_ != nullptr; }

private:
    FILE *file_ = nullptr;
    RomReadFn rom_read_ = nullptr;
    void *rom_context_ = nullptr;
    uint32_t length_ = 0;
    uint32_t position_ = 0;
};

class SdFat {
public:
    FsFile open(const char *path, int flags);
    bool exists(const char *path);
    bool remove(const char *path);
    bool rename(const char *old_path, const char *new_path);
    bool mkdir(const char *path);
};
