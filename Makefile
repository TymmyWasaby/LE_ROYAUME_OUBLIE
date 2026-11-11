.RECIPEPREFIX := |

Q ?= @
CC = arm-none-eabi-gcc
CXX = arm-none-eabi-g++
BUILD_DIR = target
NWLINK = npx --yes -- nwlink@1.0.0

CPPFLAGS = -Isrc -std=c++11 -fno-exceptions -fno-rtti
CPPFLAGS += $(shell $(NWLINK) eadk-cflags-device)
CPPFLAGS += -Os -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CPPFLAGS += -fno-math-errno -fsingle-precision-constant -fno-tree-loop-distribute-patterns
CPPFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
LDFLAGS += -Wl,--gc-sections -flinker-output=nolto-rel

.PHONY: all build clean
all: build
build: $(BUILD_DIR)/tymbrawl.nwa

$(BUILD_DIR)/tymbrawl.nwa: $(BUILD_DIR)/game.o $(BUILD_DIR)/appid.o $(BUILD_DIR)/icon.o
| @echo "LD      $@"
| $(Q) $(CC) $(CPPFLAGS) $(LDFLAGS) $^ -lm -o $@
| $(Q) arm-none-eabi-strip --strip-unneeded $@

$(BUILD_DIR)/game.o: src/game.cpp | $(BUILD_DIR)
| @echo "CC      $<"
| $(Q) $(CXX) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/appid.o: src/app_id.c | $(BUILD_DIR)
| @echo "CC      $< (identite NWA, C sans LTO)"
| $(Q) $(CC) $(shell $(NWLINK) eadk-cflags-device) -Isrc -std=c11 -Os -Wall -c $< -o $@

$(BUILD_DIR)/icon.o: src/icon.png | $(BUILD_DIR)
| @echo "ICON    $<"
| $(Q) $(NWLINK) png-icon-o $< $@

$(BUILD_DIR):
| $(Q) mkdir -p $(BUILD_DIR)

clean:
| rm -rf $(BUILD_DIR)
