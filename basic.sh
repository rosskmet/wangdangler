# 1. Clone or create project folder
mkdir MultiBandSatComp
cd MultiBandSatComp

# 2. Create directory structure
mkdir -p Source/{DSP/{Crossover,Compressor,Saturation,Oversampling,Utils},Parameters,GUI/{Components,Layouts,Visualizers}}
mkdir -p Resources/{Presets/Factory,Assets}
mkdir -p cmake/Modules tests/UnitTests tests/IntegrationTests .vscode

# 3. Copy all the files above into their respective locations

# 4. Create build directory
mkdir build && cd build

# 5. Run CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 6. Build
cmake --build . --config Debug

# 7. Test (when ready)
ctest --output-on-failure