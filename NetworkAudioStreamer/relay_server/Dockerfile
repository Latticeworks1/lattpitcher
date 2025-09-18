FROM ubuntu:22.04

LABEL maintainer="Ultra Audio Relay Server"
LABEL description="Professional real-time audio streaming relay with microsecond latency"

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libboost-dev \
    nlohmann-json3-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy source code
COPY . .

# Build application
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native" && \
    make -j$(nproc)

# Create non-root user for security
RUN useradd -r -s /bin/false relay

# Expose ports
EXPOSE 8080/tcp 9001/udp

# Set capabilities for real-time priority
RUN apt-get update && apt-get install -y libcap2-bin && \
    setcap 'cap_sys_nice=ep' /app/build/relay_server

# Switch to non-root user
USER relay

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD nc -z localhost 8080 || exit 1

# Run server
CMD ["./build/relay_server"]