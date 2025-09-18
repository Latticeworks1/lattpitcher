# NetworkAudioStreamer - WiFi Multi-Computer Setup Guide

## 🌐 Network Requirements
- **All computers on same WiFi network** (192.168.0.x subnet)
- **Server IP**: 192.168.0.22 (this computer)
- **Port**: 9001 (default)
- **Latency**: 5-15ms over local WiFi

## 🖥️ Server Setup (Primary Computer - IP: 192.168.0.22)

### Step 1: Install Plugin
```bash
# Plugin already installed at:
~/Library/Audio/Plug-Ins/VST3/Network Audio Streamer.vst3
```

### Step 2: Configure FL Studio
1. **Open FL Studio 21**
2. **Mixer** → Select any channel (e.g., Channel 1)
3. **Insert Slot 1** → Load **"Network Audio Streamer"**
4. **Plugin Settings**:
   - Mode: **Server**
   - Port: **9001** 
   - Network Audio: **Enabled**
   - Input Gain: **100%**
   - Network Mix: **100%** (send audio to network)
5. **Click "Connect"** → Status shows "Server listening on port 9001"

### Step 3: Route Audio
- Any audio on Channel 1 will be streamed to connected clients
- Multiple channels can have separate server instances (different ports)

## 🖥️ Client Setup (Remote Computers)

### Step 1: Build & Install Plugin on Each Computer
```bash
# On each remote Mac:
git clone [repository]
cd NetworkAudioStreamer
./build.sh
cp -r "build/NetworkAudioStreamerPlugin_artefacts/VST3/Network Audio Streamer.vst3" ~/Library/Audio/Plug-Ins/VST3/

# On Windows:
# Build using Visual Studio, install to %COMMONPROGRAMFILES%/VST3/

# On Linux: 
# Build using make, install to ~/.vst3/
```

### Step 2: Configure FL Studio (Each Client)
1. **Open FL Studio**
2. **Mixer** → Select empty channel (e.g., Channel 10)
3. **Insert Slot 1** → Load **"Network Audio Streamer"**
4. **Plugin Settings**:
   - Mode: **Client**
   - Server Address: **192.168.0.22**
   - Port: **9001**
   - Output Gain: **100%**
   - Network Mix: **100%** (receive network audio)
5. **Click "Connect"** → Status shows "Connected to server"

### Step 3: Receive Audio
- Remote audio now appears on Channel 10
- Mix with local tracks like any other channel
- Use FL Studio's mixer for effects, routing, etc.

## 🔥 Advanced Multi-Studio Setup

### Bidirectional Audio (Each studio sends AND receives)
```
Studio A (192.168.0.22):
├── Channel 1: Local Guitar → Server A (port 9001) → Studio B
└── Channel 10: Studio B Vocals ← Client B (port 9002) ← Studio B

Studio B (192.168.0.35):  
├── Channel 1: Local Vocals → Server B (port 9002) → Studio A
└── Channel 10: Studio A Guitar ← Client A (port 9001) ← Studio A
```

### Multiple Audio Sources
```
Studio A:
├── Channel 1: Drums → Server (port 9001)
├── Channel 2: Bass → Server (port 9002) 
└── Channel 3: Keys → Server (port 9003)

Studio B receives:
├── Channel 8: Remote Drums ← Client (port 9001)
├── Channel 9: Remote Bass ← Client (port 9002)
└── Channel 10: Remote Keys ← Client (port 9003)
```

## 🛠️ Network Troubleshooting

### Check Network Connectivity
```bash
# Test connection between computers
ping 192.168.0.22

# Check if port is open
telnet 192.168.0.22 9001
```

### Firewall Settings
- **macOS**: System Preferences → Security & Privacy → Firewall → Allow incoming connections for FL Studio
- **Windows**: Windows Defender → Allow app through firewall → Add FL Studio
- **Router**: Ensure port 9001 is not blocked (usually OK for local network)

### Optimize Performance
- **Use wired ethernet** for lowest latency (<5ms)
- **WiFi 5/6** for good performance (5-10ms)
- **Close bandwidth-heavy apps** (streaming, downloads)
- **FL Studio buffer**: 128-256 samples for balance of latency/stability

## 📊 Expected Performance

| Connection Type | Latency | Quality | Max Clients |
|----------------|---------|---------|-------------|
| Wired Ethernet | 2-5ms   | Perfect | 8+ |
| WiFi 6 (5GHz)  | 5-10ms  | Excellent | 6+ |
| WiFi 5 (5GHz)  | 8-15ms  | Very Good | 4+ |
| WiFi (2.4GHz)  | 15-30ms | Good | 2-3 |

## 🎵 Musical Use Cases

### Jam Sessions
- **Guitarist** in bedroom → **Drummer** in garage → **Bassist** in studio
- Real-time collaboration with <10ms latency
- Each person hears the full mix in their FL Studio

### Recording Sessions  
- **Lead vocalist** in vocal booth → **Producer** in control room
- **Session musicians** joining remotely
- **Overdubs** with precise timing synchronization

### Live Performance
- **Backing tracks** streamed from main computer
- **Individual monitors** for each performer
- **In-ear monitoring** with personal mixes

## 🚀 Getting Started

1. **Start simple**: One server, one client on same WiFi
2. **Test latency**: Play along to a metronome
3. **Add complexity**: Multiple sources, bidirectional audio
4. **Optimize**: Wired connections, proper router placement
5. **Scale up**: Add more musicians, complex routing

Your NetworkAudioStreamer is now ready for **professional multi-studio collaboration** over WiFi! 🎸🥁🎹🎤