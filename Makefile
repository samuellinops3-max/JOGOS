TARGET = cobra_arena
TITLE = COBRA ARENA
APPID = COBRA00001
OBJS = main.o
CFLAGS = -O2 -Wall -Wno-error -mcpu=cell
LIBS = -lrsx -lgcm_sys -lsysutil -lio -lsysmodule -lm
include $(PSL1GHT)/ppu_rules