.DEFAULT_GOAL := all
.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is unset; run ps3/build.ps1)
endif
include $(PSL1GHT)/ppu_rules

BUILD := build/ps3
GEN := work/generated_c
TARGET := $(BUILD)/slaythespire
APPID := STSP00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
TITLE := Slay the Spire PS3
ASSETS ?= work/assets_vita
PS3_SRC := $(wildcard ps3/platform/*.c ps3/gfx/*.c) native/src/glue/misc_glue.c native/src/glue/freetype_glue.c
JAVA_SRC := $(addprefix $(GEN)/,$(shell cat $(GEN)/all.txt 2>/dev/null))
OBJECTS := $(addprefix $(BUILD)/obj/,$(PS3_SRC:.c=.o) $(JAVA_SRC:.c=.o))
SHADER_OBJ := $(BUILD)/shaders/menu.vpo.o $(BUILD)/shaders/menu.fpo.o
SHADER_HDR := $(BUILD)/shaders/menu_vpo.h $(BUILD)/shaders/menu_fpo.h
INC := -Ips3/include -Ips3/gfx -Inative/src/glue -I$(BUILD)/shaders -I$(PORTLIBS)/include -I$(PORTLIBS)/include/freetype2 $(LIBPSL1GHT_INC)
FLAGS := -O2 -fno-strict-aliasing -mcpu=cell -std=gnu11 -D_GNU_SOURCE -DTEAVM_PS3=1 $(MACHDEP) $(INC) -include $(GEN)/uchar.h -include $(GEN)/classes/java/lang/Class.h
LIBRARIES := -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB) -lfreetype -lpng -lz -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm

.PHONY: all pkg
all: $(TARGET).self
$(BUILD)/obj/$(GEN)/%.o: $(GEN)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -Wno-discarded-qualifiers -Dmain=stsv_java_main -MMD -MP -c $< -o $@

$(BUILD)/obj/%.o: %.c $(SHADER_HDR)
	@mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -Wall -MMD -MP -c $< -o $@

$(BUILD)/shaders/menu.vpo: ps3/shaders/menu.vcg
	@mkdir -p $(dir $@)
	$(CGCOMP) -v $< $@

$(BUILD)/shaders/menu.fpo: ps3/shaders/menu.fcg
	@mkdir -p $(dir $@)
	$(CGCOMP) -f $< $@

$(BUILD)/shaders/%.vpo.o: $(BUILD)/shaders/%.vpo
	@cd $(dir $@) && bin2s -a 64 $(notdir $<) | $(AS) -o $(notdir $@)

$(BUILD)/shaders/%.fpo.o: $(BUILD)/shaders/%.fpo
	@cd $(dir $@) && bin2s -a 64 $(notdir $<) | $(AS) -o $(notdir $@)

$(BUILD)/shaders/menu_vpo.h: $(BUILD)/shaders/menu.vpo
	@printf 'extern const unsigned char menu_vpo[];\nextern const unsigned int menu_vpo_size;\n' > $@

$(BUILD)/shaders/menu_fpo.h: $(BUILD)/shaders/menu.fpo
	@printf 'extern const unsigned char menu_fpo[];\nextern const unsigned int menu_fpo_size;\n' > $@

$(TARGET).elf: $(OBJECTS) $(SHADER_OBJ)
	@echo Linking original game for PPU
	@$(CC) $(MACHDEP) -Wl,-Map,$(TARGET).map -o $@ $(OBJECTS) $(SHADER_OBJ) $(LIBRARIES)
	$(SPRX) $@

$(TARGET).self: $(TARGET).elf
	$(SELF) $< $@

# The PSL1GHT pkg tool aborts past ~1024 entries and the assets alone are
# ~3700, so the PKG carries only the program. Copy work/assets_vita to
# USRDIR/assets separately (run-rpcs3.ps1 does it for RPCS3).
pkg: all
	rm -rf $(BUILD)/pkg
	mkdir -p $(BUILD)/pkg/USRDIR
	$(SELF_NPDRM) $(TARGET).elf $(BUILD)/pkg/USRDIR/EBOOT.BIN $(CONTENTID)
	$(SFO) --title "$(TITLE)" --appid $(APPID) -f $(SFOXML) $(BUILD)/pkg/PARAM.SFO
	cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	$(PKG) --contentid $(CONTENTID) $(BUILD)/pkg/ $(TARGET).pkg

-include $(OBJECTS:.o=.d)
