FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential openmpi-bin libopenmpi-dev libssl-dev python3 \
    python3-matplotlib python3-numpy python3-pandas ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /proyecto
ENV OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
CMD ["bash"]
