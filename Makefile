#---------------------------------------------------------------------------------
# Makefile - Cobra Arena (PS3 Homebrew / PSL1GHT + SDL 1.2)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error "Configure a variavel de ambiente PSL1GHT antes de compilar.")
endif

include $(PSL1GHT)/ppu_rules

#---------------------------------------------------------------------------------
# Configuracoes do Projeto
#---------------------------------------------------------------------------------
TARGET      := cobra_arena
BUILD       := build
SOURCES     := .
INCLUDES    := .

# Configuracoes do PKG (caso rode make pkg)
TITLE       := Cobra Arena
APPID       := SLITHER01
CONTENTID   := UP0001-$(APPID)_00-0000000000000000

#---------------------------------------------------------------------------------
# Flags de Compilacao (Otimizado para 60 FPS no Cell/PPU)
#---------------------------------------------------------------------------------
CFLAGS      := -O3 -Wall -ffast-math -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS    := $(CFLAGS)
LDFLAGS     := $(MACHDEP) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# Bibliotecas (SDL 1.2 + Controles + Sistema PS3 + Matematica)
#---------------------------------------------------------------------------------
LIBS        := -lSDL -laudio -lio -lgcm_sys -lreality -lsysutil -lsysmodule -lm

#---------------------------------------------------------------------------------
# Caminhos de Cabecalhos e Bibliotecas do PSL1GHT / Portlibs
#---------------------------------------------------------------------------------
LIBDIRS     := $(PORTLIBS) $(PSL1GHT)

#---------------------------------------------------------------------------------
# Regras Automaticas de Compilacao
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include/SDL) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean pkg

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo Limpando arquivos de compilacao...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).self $(TARGET).pkg

pkg: $(BUILD)
	@echo Gerando pacote PKG...
	@make_self_npdrm $(OUTPUT).elf $(OUTPUT).self $(CONTENTID)
	@mkdir -p pkg/USRDIR
	@cp $(OUTPUT).self pkg/USRDIR/EBOOT.BIN
	@sfo.py --title "$(TITLE)" --appid "$(APPID)" -f $(PSL1GHT)/bin/sfo.xml pkg/PARAM.SFO
	@pkg.py --contentid $(CONTENTID) pkg/ $(OUTPUT).pkg

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
