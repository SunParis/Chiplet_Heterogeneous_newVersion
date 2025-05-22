.PHONY: all build_dir configure build clean

all: build_dir configure build

build_dir:
	@if [ ! -d "build" ]; then \
	    mkdir build; \
	fi

configure: build_dir
	@cd build && cmake ..

build: configure
	@cd build && make -j6

clean:
	@if [ -d "build" ]; then \
	    cd build && make clean; \
	fi

