@echo off
set PS5_PAYLOAD_SDK=C:\Users\Blurf\ps5dev\toolchains\ps5-payload-sdk
set PATH=%PS5_PAYLOAD_SDK%\bin;%PATH%
make CC=prospero-clang CXX=prospero-clang++
