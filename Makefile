# Makefile wrapper around Meson/Ninja workflow.
# Keep Meson as source of truth; this file only shortens common commands.

.PHONY: help release strict asan asan-strict coverage clangd-db test-release test-asan unit unit-all clean clean-release clean-strict clean-asan clean-asan-strict clean-coverage

RELEASE_DIR := build
STRICT_DIR := build-strict
ASAN_DIR := build-asan
ASAN_STRICT_DIR := build-asan-strict
COVERAGE_DIR := build-coverage

help:
	@echo "Targets:"
	@echo "  make release       Configure + compile release build"
	@echo "  make strict        Configure + compile strict release build (-Werror, -march=native)"
	@echo "  make asan          Configure + compile ASAN build"
	@echo "  make asan-strict   Configure + compile strict ASAN build"
	@echo "  make coverage      Build with coverage + generate $(COVERAGE_DIR)/lcov.info"
	@echo "  make clangd-db     Generate compile_commands.json for clangd"
	@echo "  make test-release  Run tests in release build dir"
	@echo "  make test-asan     Run tests in ASAN build dir"
	@echo "  make unit          Configure + run unit tests"
	@echo "  make unit-all      Configure + run all registered unit tests"
	@echo "  make clean-release Remove release build dir"
	@echo "  make clean-strict  Remove strict build dir"
	@echo "  make clean-asan    Remove ASAN build dir"
	@echo "  make clean-asan-strict Remove strict ASAN build dir"
	@echo "  make clean-coverage Remove coverage build dir"
	@echo "  make clean         Remove all build dirs"

release:
	meson setup $(RELEASE_DIR) --reconfigure --buildtype=release -Db_sanitize=none
	meson compile -C $(RELEASE_DIR)
	@tmp_bin="$(RELEASE_DIR)/zestwm.stripped"; \
	strip -o "$${tmp_bin}" "$(RELEASE_DIR)/zestwm" && mv -f "$${tmp_bin}" "$(RELEASE_DIR)/zestwm"

strict:
	meson setup $(STRICT_DIR) --reconfigure --buildtype=release -Doptimization=3 -Db_sanitize=none -Dstrict_cpp=true -Dnative_optimizations=true
	meson compile -C $(STRICT_DIR)
	@tmp_bin="$(STRICT_DIR)/zestwm.stripped"; \
	strip -o "$${tmp_bin}" "$(STRICT_DIR)/zestwm" && mv -f "$${tmp_bin}" "$(STRICT_DIR)/zestwm"

asan:
	meson setup $(ASAN_DIR) --reconfigure --buildtype=debug -Db_sanitize=address,undefined
	meson compile -C $(ASAN_DIR)

asan-strict:
	meson setup $(ASAN_STRICT_DIR) --reconfigure --buildtype=debug -Db_sanitize=address,undefined -Dstrict_cpp=true -Dnative_optimizations=true
	meson compile -C $(ASAN_STRICT_DIR)

coverage:
	command -v lcov >/dev/null
	meson setup $(COVERAGE_DIR) --reconfigure --buildtype=debug -Db_coverage=true -Dunit_tests=true -Dintegration_tests=true
	meson compile -C $(COVERAGE_DIR)
	meson test -C $(COVERAGE_DIR)
	lcov --capture --directory $(COVERAGE_DIR) --output-file $(COVERAGE_DIR)/lcov.info
	lcov --remove $(COVERAGE_DIR)/lcov.info '/usr/*' '*/subprojects/*' --output-file $(COVERAGE_DIR)/lcov.info

clangd-db:
	meson setup $(RELEASE_DIR) --reconfigure --buildtype=release -Db_sanitize=none

test-release:
	meson compile -C $(RELEASE_DIR)
	meson test -C $(RELEASE_DIR)
	meson test -C $(RELEASE_DIR) --num-processes 1 focusurgent focusurgent-no-urgent

test-asan:
	meson compile -C $(ASAN_DIR)
	meson test -C $(ASAN_DIR)
	meson test -C $(ASAN_DIR) --num-processes 1 focusurgent focusurgent-no-urgent

unit:
	meson setup $(RELEASE_DIR) --reconfigure --buildtype=release -Db_sanitize=none -Dunit_tests=true -Dintegration_tests=false
	meson compile -C $(RELEASE_DIR)
	meson test -C $(RELEASE_DIR) zestctl-split-ws config-parse config-sections config-windowrule config-workspace config-env

unit-all:
	meson setup $(RELEASE_DIR) --reconfigure --buildtype=release -Db_sanitize=none -Dunit_tests=true -Dintegration_tests=false
	meson compile -C $(RELEASE_DIR)
	meson test -C $(RELEASE_DIR)

clean-release:
	rm -rf $(RELEASE_DIR)

clean-strict:
	rm -rf $(STRICT_DIR)

clean-asan:
	rm -rf $(ASAN_DIR)

clean-asan-strict:
	rm -rf $(ASAN_STRICT_DIR)

clean-coverage:
	rm -rf $(COVERAGE_DIR)

clean: clean-release clean-strict clean-asan clean-asan-strict clean-coverage
