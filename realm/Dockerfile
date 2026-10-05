# Living Azeroth toolchain and runtime image: builds the server and runs it.
FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ccache cmake ninja-build clang-18 lld-18 libclang-rt-18-dev git ca-certificates \
    curl unzip default-mysql-client default-libmysqlclient-dev libboost-all-dev \
    libssl-dev libmysql++-dev libreadline-dev zlib1g-dev libbz2-dev libncurses-dev \
    liblzma-dev python3 python3-pymysql python3-cryptography time \
    && rm -rf /var/lib/apt/lists/*
ENV CCACHE_DIR=/cache/ccache CCACHE_MAXSIZE=4G CCACHE_COMPILERCHECK=content
ENV GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0=/source
WORKDIR /realm
