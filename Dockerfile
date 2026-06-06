FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    iverilog \
    verilator \
    gtkwave \
    make \
    gcc \
    python3 \
    python3-pip \
    git \
    ca-certificates \
    bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /project

COPY . /project

RUN chmod +x /project/sim/run_sim.sh

CMD ["make", "sim"]
