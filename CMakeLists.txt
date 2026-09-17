cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(FoloToy-AI-Passport)

# 单独生成首次安装用的空白存档文件系统；常规 flash 不覆盖已有存档。
spiffs_create_partition_image(saves assets/initial_saves)
