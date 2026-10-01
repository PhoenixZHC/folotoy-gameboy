cmake_minimum_required(VERSION 3.16)
# 用户确认的产品版本；设置页从固件描述信息读取，避免重复维护。
set(PROJECT_VER "1.3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(FoloToy-AI-Passport)

# 单独生成首次安装用的空白存档文件系统；常规 flash 不覆盖已有存档。
spiffs_create_partition_image(saves assets/initial_saves)
