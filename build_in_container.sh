#!/bin/bash
set -e

CLEAN=false
RECONFIGURE=false

for arg in "$@"; do
    case $arg in
        --clean|-c)
            CLEAN=true
            ;;
        --reconfigure|-r)
            RECONFIGURE=true
            ;;
    esac
done

echo "Starting build inside Ubuntu container..."

# Compile dependencies (only if prefix directory doesn't exist or reconfigure requested)
if [ ! -d "depends/x86_64-pc-linux-gnu" ] || $RECONFIGURE; then
    echo "Compiling dependencies..."
    cd depends
    chmod +x config.sub config.guess
    make -j30 HOST=x86_64-pc-linux-gnu
    cd ..
fi

# Configure KristaTech (only if Makefile doesn't exist or reconfigure requested)
if [ ! -f "Makefile" ] || $RECONFIGURE; then
    echo "Configuring build system..."
    chmod +x share/genbuild.sh autogen.sh
    ./autogen.sh
    ./configure --enable-glibc-back-compat \
                --prefix=$(pwd)/depends/x86_64-pc-linux-gnu \
                LDFLAGS="-static-libstdc++" \
                --enable-cxx \
                --enable-static \
                --disable-shared \
                --disable-debug \
                --disable-bench \
                --with-pic \
                CPPFLAGS="-fPIC -O3 --param ggc-min-expand=1 --param ggc-min-heapsize=32768" \
                CXXFLAGS="-fPIC -O3 --param ggc-min-expand=1 --param ggc-min-heapsize=32768"
fi

if $CLEAN; then
    echo "Cleaning build..."
    make clean
fi

# Compile KristaTech
echo "Compiling KristaTech..."
make -j24 HOST=x86_64-pc-linux-gnu

echo "Build completed successfully!"
