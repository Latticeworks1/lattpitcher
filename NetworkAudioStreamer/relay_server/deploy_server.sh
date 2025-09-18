#!/bin/bash

# Deployable Audio Relay Server Builder
# Creates minimal binary for VPS deployment

echo "🚀 Building deployable audio relay server..."

# Static build for deployment
mkdir -p build_deploy && cd build_deploy

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-static-libgcc -static-libstdc++" \
    -DBoost_USE_STATIC_LIBS=ON

make -j$(nproc)

# Package for deployment
mkdir -p relay_deploy
cp relay_server relay_deploy/
cp ../deploy_config.json relay_deploy/

# Create startup script
cat > relay_deploy/start_relay.sh << 'EOF'
#!/bin/bash
echo "Starting Audio Relay Server..."
echo "WebSocket Control: Port 8080"
echo "UDP Audio Relay: Port 9001"
echo "Press Ctrl+C to stop"
./relay_server
EOF

chmod +x relay_deploy/start_relay.sh

echo "✅ Deployable server ready in relay_deploy/"
echo "🌐 Deploy to VPS: scp -r relay_deploy user@your-vps.com:~/"
echo "🚀 Run on VPS: ./start_relay.sh"

ls -la relay_deploy/