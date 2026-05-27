cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$HOME/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
./build/E8Emulator