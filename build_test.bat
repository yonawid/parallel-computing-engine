@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build-Release -DCMAKE_BUILD_TYPE=Release
cmake --build build-Release --config Release
cd build-Release
ctest -C Release --output-on-failure --repeat until-fail:100
