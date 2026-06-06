IMAGE_NAME ?= zboard-softstarter-dev
CONTAINER_WORKDIR ?= /project

RTL_SRCS := rtl/sync_generator.v rtl/phase_counter.v rtl/control_angle.v rtl/top.v
TB_SRCS := tb/tb_top.v
BUILD_DIR := build
SIM_OUT := $(BUILD_DIR)/soft_starter_tb.out
VCD := $(BUILD_DIR)/waves.vcd

.PHONY: all sim wave lint clean docker-build docker-sim docker-shell

all: sim

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIM_OUT): $(RTL_SRCS) $(TB_SRCS) | $(BUILD_DIR)
	iverilog -g2012 -I rtl -o $(SIM_OUT) $(RTL_SRCS) $(TB_SRCS)

sim: $(SIM_OUT)
	vvp $(SIM_OUT)

wave: sim
	gtkwave $(VCD) sim/soft_starter.gtkw

lint:
	verilator --lint-only -Irtl --top-module top $(RTL_SRCS)

clean:
	rm -rf $(BUILD_DIR)

docker-build:
	docker build -t $(IMAGE_NAME) .

docker-sim:
	docker run --rm -v "$(PWD):$(CONTAINER_WORKDIR)" -w $(CONTAINER_WORKDIR) $(IMAGE_NAME) make sim

docker-shell:
	docker run --rm -it -v "$(PWD):$(CONTAINER_WORKDIR)" -w $(CONTAINER_WORKDIR) $(IMAGE_NAME) bash
