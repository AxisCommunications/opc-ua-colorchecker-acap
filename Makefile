TARGET = opcuacolorchecker
OBJECTS = $(wildcard $(CURDIR)/src/*.cpp)
RM ?= rm -f

SDK_PKGS = axevent axparameter fcgi gio-2.0 gio-unix-2.0 open62541 vdostream
OWN_PKGS = opencv4 open62541

CXXFLAGS += -Os -pipe -std=c++20 -Wall -Werror -Wextra
CXXFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags-only-I $(SDK_PKGS))
CXXFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) PKG_CONFIG_SYSROOT_DIR= pkg-config --cflags-only-I $(OWN_PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(SDK_PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) PKG_CONFIG_SYSROOT_DIR= pkg-config --libs $(SDK_PKGS) $(OWN_PKGS))

CXXFLAGS += -I$(CURDIR)/include
LDFLAGS = -L./lib -Wl,--no-as-needed,-rpath,'$$ORIGIN/lib' -flto=auto
#LDLIBS += -lm -lpthread

# Set DEBUG_WRITE to write debug images to storage
ifneq ($(DEBUG_WRITE),)
CXXFLAGS += -DDEBUG_WRITE
DOCKER_ARGS += --build-arg DEBUG_WRITE=$(DEBUG_WRITE)
endif

.PHONY: all %.eap dockerbuild clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS) && \
	$(STRIP) --strip-unneeded $@

# Docker build container targets
%.eap:
	DOCKER_BUILDKIT=1 docker build $(DOCKER_ARGS) --build-arg ARCH=$(*F) -o type=local,dest=. "$(CURDIR)"

dockerbuild: aarch64.eap armv7hf.eap

clean:
	$(RM) $(TARGET) *.eap* *_LICENSE.txt pa*.conf
