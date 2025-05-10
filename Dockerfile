# syntax=docker/dockerfile:1.7

FROM mstorsjo/llvm-mingw:20250430@sha256:e792d52b027fbe7c8831cc7d6f27882332789b0d07a0f5a1a96532a4ef27211a AS llvm-mingw

FROM llvm-mingw AS stage0

RUN \
    --mount=type=bind,target=/root/src \
    --mount=type=cache,target=/root/build \
<<'EOF'

set -eux

cmake -S /root/src -B /root/build -G "Ninja Multi-Config" \
    -DCMAKE_VERBOSE_MAKEFILE="on" \
    -DCMAKE_SYSTEM_NAME="Windows" \
    -DCMAKE_CXX_COMPILER="x86_64-w64-mingw32-clang++" \
    -DCMAKE_CXX_FLAGS_INIT="-v"

cmake --build /root/build --config Release -v

cmake --install /root/build --prefix /root/install --config Release -v

EOF

FROM scratch
COPY --from=stage0 /root/install /
