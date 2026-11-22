.PHONY: all build build-debug test test-api test-all clean run-engine run-api run-ui lint format benchmark

BUILD_DIR := build
ENGINE_BIN := $(BUILD_DIR)/engine/exchange_engine

all: build

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$$(nproc)

build-debug:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON .. && make -j$$(nproc)

test: build-debug
	@cd $(BUILD_DIR) && ctest --output-on-failure

test-api: build
	@cd api && ENGINE_PATH=$(CURDIR)/$(ENGINE_BIN) python -m pytest tests/ -v

test-all: test test-api

run-engine: build
	@$(ENGINE_BIN)

run-api: build
	@cd api && python -m uvicorn app.main:app --reload

CPP_SOURCES = find engine/src engine/include engine/tests benchmarks \( -name '*.cpp' -o -name '*.hpp' \) -not -path '*/nlohmann/*'

lint:
	@$(CPP_SOURCES) | xargs clang-format --dry-run -Werror
	@cd api && python -m ruff check .

format:
	@$(CPP_SOURCES) | xargs clang-format -i
	@cd api && python -m ruff check --fix .

clean:
	@rm -rf $(BUILD_DIR)
	@find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true

# Run from /tmp: the logging scenarios write their event log to the working directory.
benchmark:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON .. && make -j$$(nproc) exchange_benchmark
	@cd /tmp && taskset -c 2 $(CURDIR)/$(BUILD_DIR)/engine/exchange_benchmark

run-ui:
	@cd ui && npm install && npm run dev
