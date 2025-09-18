#!/bin/bash

# Ultra-Fast Audio Relay Server - Release Builder
# Creates optimized production binary

set -e

echo "🚀 Building Ultra-Fast Audio Relay Server..."

# Check dependencies
command -v cmake >/dev/null 2>&1 || { echo "❌ cmake required but not installed"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "❌ g++ required but not installed"; exit 1; }

# Clean previous builds
rm -rf build_release
mkdir -p build_release
cd build_release

# Configure with maximum optimizations
echo "⚙️  Configuring build with maximum optimizations..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native -flto -DNDEBUG" \
    -DCMAKE_EXE_LINKER_FLAGS="-flto" \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON

# Build with all CPU cores
echo "🔨 Building with $(nproc) CPU cores..."
make -j$(nproc)

# Verify binary exists
if [ ! -f relay_server ]; then
    echo "❌ Build failed - binary not found"
    exit 1
fi

# Strip debug symbols for smaller binary
echo "🔧 Stripping debug symbols..."
strip relay_server

# Check binary size and dependencies
echo "📊 Binary information:"
ls -lh relay_server
file relay_server
ldd relay_server 2>/dev/null || otool -L relay_server 2>/dev/null || echo "No dynamic dependencies shown"

# Create deployment package
echo "📦 Creating deployment package..."
cd ..
mkdir -p relay_package

# Copy binary and configs
cp build_release/relay_server relay_package/
cp deploy_config.json relay_package/
cp README.md relay_package/
cp *.sh relay_package/ 2>/dev/null || true

# Create startup script
cat > relay_package/start_server.sh << 'EOF'
#!/bin/bash

echo "🎵 Starting Ultra-Fast Audio Relay Server..."
echo "Features: Lock-free, Zero-copy, Memory-mapped, Real-time priority"
echo ""
echo "Ports:"
echo "  WebSocket Control: 8080/tcp"
echo "  UDP Audio Relay:   9001/udp"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Check if running as root for real-time priority
if [ "$EUID" -ne 0 ]; then
    echo "⚠️  Warning: Not running as root - real-time priority disabled"
    echo "   For maximum performance, run: sudo ./start_server.sh"
    echo ""
fi

./relay_server
EOF

chmod +x relay_package/start_server.sh
chmod +x relay_package/relay_server

# Create archive
echo "📁 Creating release archive..."
tar -czf ultra-audio-relay-$(date +%Y%m%d).tar.gz relay_package/

echo ""
echo "✅ Build completed successfully!"
echo ""
echo "📦 Release package: relay_package/"
echo "📁 Archive: ultra-audio-relay-$(date +%Y%m%d).tar.gz"
echo ""
echo "🚀 To deploy:"
echo "   1. Copy relay_package/ to your server"
echo "   2. Run: sudo ./start_server.sh"
echo ""
echo "🌐 For Docker deployment:"
echo "   docker build -t ultra-audio-relay ."
echo "   docker run -p 8080:8080/tcp -p 9001:9001/udp ultra-audio-relay"
echo ""