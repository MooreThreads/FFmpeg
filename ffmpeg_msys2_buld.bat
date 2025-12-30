set MSYS2_PATH_TYPE=inherit
set "current_path=%~dp0"
set "current_linux_path=%current_path:\=/%"
echo %current_linux_path%
C:\msys64\msys2_shell.cmd -mingw64 -defterm -no-start -c "cd %current_linux_path% && ./configure --enable-libdav1d --prefix=./build-msvc --arch=x86 --disable-asm --enable-static --enable-w32threads --disable-pthreads --disable-shared --disable-bzlib --disable-lzma --disable-iconv  --disable-zlib --disable-doc  --extra-cflags="-DPTW32_STATIC_LIB"  --extra-ldflags="-static" && make clean && make -j12 && make install"