#common makefile header

DIR_INC = include
DIR_BIN = bin
DIR_LIB = libs

TARGET	= xiuxiu
BIN_TARGET = $(DIR_BIN)/$(TARGET)

CROSS_COMPILE = 
CFLAGS = -g -Wall -I$(DIR_INC) -I/usr/include/libxml2

ifdef LINUX64
LDFLAGS := -L$(DIR_LIB)/x64
else
LDFLAGS := -L$(DIR_LIB)/x86 
endif
LDFLAGS += -lmsc -lrt -ldl -lpthread -lasound -lstdc++ -lxml2

#OBJECTS := $(patsubst %.c,%.o,$(wildcard *.c))
#OBJECTS := xiuxiu.o linuxrec.o speech_recognizer.o
OBJECTS := test.o awaken.o linuxrec.o speech_recognizer.o tts_offline_sample.o sound_playback.o

$(BIN_TARGET) : $(OBJECTS)
	$(CROSS_COMPILE)gcc $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o : %.c
	$(CROSS_COMPILE)gcc -c $(CFLAGS) $< -o $@
clean:
	@rm -f *.o $(BIN_TARGET)

.PHONY:clean

#common makefile foot
