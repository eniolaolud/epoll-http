.PHONY: all build run smoke test clean

BUILD_DIR ?= build
PORT ?= 38491

all: build

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
	cmake --build $(BUILD_DIR) -j

run: build
	./$(BUILD_DIR)/http_server --host 0.0.0.0 --port $(PORT) --www www --cgi cgi-bin

smoke:
	./scripts/smoke.sh http://127.0.0.1:$(PORT)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	./scripts/smoke.sh http://127.0.0.1:$(PORT)

clean:
	rm -rf $(BUILD_DIR)
