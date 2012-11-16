CC = gcc
CFLAGS = -std=c99 -Wall -O2 -I/usr/local/natinst/nidaqmxbase/include/
CFLAGS += -DGIT_VERSION=\"$(VER)\"
LDLIBS = -lnidaqmxbase
VER = $(shell git rev-parse HEAD)$(shell [[ -z $$(git diff-index --name-only HEAD --) ]] || echo -n "-dirty")

all: simplemeter

clean:
	rm simplemeter

