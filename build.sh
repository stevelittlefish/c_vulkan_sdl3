#!/bin/bash

# Parse command line arguments
ENABLE_VALIDATION=""
BUILD_TYPE="Debug"

for arg in "$@"; do
    case $arg in
        --validation)
            ENABLE_VALIDATION="-DENABLE_VALIDATION_LAYERS=ON"
            ;;
        --release)
            BUILD_TYPE="Release"
            ;;
    esac
done

mkdir -p build

./update_file_list.sh

cd build
cmake -D CMAKE_BUILD_TYPE=$BUILD_TYPE $ENABLE_VALIDATION .. || { echo "ERROR: cmake failed"; exit 1; }
make || { echo "ERROR: make failed"; exit 2; }

