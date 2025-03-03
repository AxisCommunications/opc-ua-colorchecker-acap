ARG ARCH=aarch64
ARG SDK_VERSION=12.2.0
ARG SDK_IMAGE=docker.io/axisecp/acap-native-sdk
ARG DEBUG_WRITE
ARG BUILD_DIR=/opt/build
ARG ACAP_BUILD_DIR="$BUILD_DIR"/app
ARG OPEN62541_VERSION=1.4.4
ARG OPENCV_VERSION=4.11.0

FROM $SDK_IMAGE:$SDK_VERSION-$ARCH AS builder

# Set general arguments
ARG ARCH
ARG ACAP_BUILD_DIR
ARG BUILD_DIR
ARG OPEN62541_VERSION
ARG OPENCV_VERSION
ARG DEBUG_WRITE
ENV DEBUG_WRITE=$DEBUG_WRITE

# Install additional build dependencies
RUN DEBIAN_FRONTEND=noninteractive \
    apt-get update && apt-get install -y -f --no-install-recommends \
    cmake

# OpenCV
ARG OPENCV_DIR="$BUILD_DIR"/opencv
ARG OPENCV_SRC_DIR="$OPENCV_DIR"/opencv-$OPENCV_VERSION
ARG OPENCV_BUILD_DIR="$OPENCV_DIR"/build

WORKDIR "$OPENCV_DIR"
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN curl -L https://github.com/opencv/opencv/archive/$OPENCV_VERSION.tar.gz | tar xz

WORKDIR "$OPENCV_BUILD_DIR"
ENV COMMON_CMAKE_FLAGS="-S $OPENCV_SRC_DIR \
        -B $OPENCV_BUILD_DIR \
        -D CMAKE_BUILD_TYPE=RELEASE \
        -D BUILD_DOCS=OFF \
        -D BUILD_EXAMPLES=OFF \
        -D BUILD_JPEG=ON \
        -D BUILD_LIST=core,video,imgproc${DEBUG_WRITE:+,imgcodecs} \
        -D BUILD_OPENCV_APPS=OFF \
        -D BUILD_PNG=OFF \
        -D BUILD_PROTOBUF=OFF \
        -D WITH_FFMPEG=OFF \
        -D WITH_GSTREAMER_0_10=OFF \
        -D WITH_GSTREAMER=OFF \
        -D WITH_GTK=OFF \
        -D WITH_JASPER=OFF \
        -D WITH_OPENEXR=OFF \
        -D WITH_V4L=OFF \
        -D OPENCV_GENERATE_PKGCONFIG=ON "

# hadolint ignore=SC2086
RUN case "$ARCH" in \
      armv7hf) \
        # Source SDK environment to get cross compilation tools
        . /opt/axis/acapsdk/environment-setup* && \
        # Configure build
        cmake \
        -D CMAKE_CXX_COMPILER=${TARGET_PREFIX}g++ \
        -D CMAKE_CXX_FLAGS="${CXX#*-g++}" \
        -D CMAKE_C_COMPILER=${TARGET_PREFIX}gcc \
        -D CMAKE_C_FLAGS="${CC#*-gcc}" \
        -D CMAKE_TOOLCHAIN_FILE=${OPENCV_SRC_DIR}/platforms/linux/arm-gnueabi.toolchain.cmake \
        -D CPU_BASELINE=NEON,VFPV3 \
        -D ENABLE_NEON=ON \
        -D ENABLE_VFPV3=ON \
	-D CMAKE_INSTALL_PREFIX="$SDKTARGETSYSROOT"/usr \
        $COMMON_CMAKE_FLAGS \
	;; \
      aarch64) \
        # Source SDK environment to get cross compilation tools
        . /opt/axis/acapsdk/environment-setup* && \
        # Configure build
        # No need to set NEON and VFP for aarch64 since they are implicitly
        # present in an any standard armv8-a implementation.
        cmake \
        -D CMAKE_CXX_COMPILER=${TARGET_PREFIX}g++ \
        -D CMAKE_CXX_FLAGS="${CXX#*-g++}" \
        -D CMAKE_C_COMPILER=${TARGET_PREFIX}gcc \
        -D CMAKE_C_FLAGS="${CC#*-gcc}" \
        -D CMAKE_TOOLCHAIN_FILE="$OPENCV_SRC_DIR"/platforms/linux/aarch64-gnu.toolchain.cmake \
	-D CMAKE_INSTALL_PREFIX="$SDKTARGETSYSROOT"/usr \
        $COMMON_CMAKE_FLAGS \
	;; \
      *) \
        printf "Error: '%s' is not a valid value for the ARCH variable\n", "$ARCH"; \
        exit 1; \
	;; \
    esac
RUN cmake --build . -j "$(nproc)" --target install/strip

# open62541
ARG OPEN62541_DIR="$BUILD_DIR"/open62541
ARG OPEN62541_SRC_DIR="$OPEN62541_DIR"/open62541-$OPEN62541_VERSION
ARG OPEN62541_BUILD_DIR="$OPEN62541_DIR"/build

WORKDIR "$OPEN62541_DIR"
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN curl -L https://github.com/open62541/open62541/archive/refs/tags/v$OPEN62541_VERSION.tar.gz | tar xz
WORKDIR "$OPEN62541_BUILD_DIR"
RUN . /opt/axis/acapsdk/environment-setup* && \
    cmake \
    -DCMAKE_INSTALL_PREFIX="$SDKTARGETSYSROOT"/usr \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BUILD_EXAMPLES=OFF \
    -DBUILD_SHARED_LIBS=ON \
    -DUA_ENABLE_NODEMANAGEMENT=ON \
    "$OPEN62541_SRC_DIR"
RUN cmake --build . -j "$(nproc)" --target install/strip

# Copy the built library files to application directory
WORKDIR "$ACAP_BUILD_DIR"/lib
RUN cp -P "$OPENCV_BUILD_DIR"/lib/lib*.so* "$OPEN62541_BUILD_DIR"/bin/lib* ./

# Build ACAP
WORKDIR "$ACAP_BUILD_DIR"
COPY LICENSE \
    Makefile \
    manifest.json \
    ./
COPY html/ ./html
COPY include/ ./include
COPY src/ ./src
RUN . /opt/axis/acapsdk/environment-setup* && acap-build .

FROM scratch
ARG ACAP_BUILD_DIR
COPY --from=builder "$ACAP_BUILD_DIR"/*eap "$ACAP_BUILD_DIR"/*LICENSE.txt /
