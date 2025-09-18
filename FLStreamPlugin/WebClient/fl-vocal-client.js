/**
 * FL Studio Vocal Client - Professional WebSocket Audio Streaming
 * Real-time vocal collaboration client for FL Studio
 */

class FLStudioVocalClient {
    constructor() {
        this.websocket = null;
        this.audioContext = null;
        this.mediaStream = null;
        this.workletNode = null;
        this.audioBufferManager = new AudioBufferManager();
        
        // Connection state
        this.isConnected = false;
        this.isRecording = false;
        this.isPaused = false;
        this.roomId = '';
        this.userId = '';
        this.userName = '';
        
        // Audio processing
        this.inputGain = 1.0;
        this.monitorVolume = 0.5;
        this.noiseGateThreshold = -40;
        this.lowCutFreq = 80;
        
        // UI elements
        this.inputMeter = null;
        this.outputMeter = null;
        
        // Statistics
        this.stats = {
            packetsReceived: 0,
            packetsSent: 0,
            bytesReceived: 0,
            bytesSent: 0,
            latency: 0,
            startTime: Date.now()
        };
        
        this.initializeUI();
        this.setupEventListeners();
    }
    
    initializeUI() {
        // Initialize audio meters
        this.inputMeter = new AudioMeter('inputMeter');
        this.outputMeter = new AudioMeter('outputMeter');
        
        // Set initial values
        document.getElementById('inputGainValue').textContent = '100%';
        document.getElementById('monitorVolumeValue').textContent = '50%';
        document.getElementById('noiseGateValue').textContent = '-40 dB';
        document.getElementById('lowCutValue').textContent = '80 Hz';
        
        this.log('info', 'FL Studio Vocal Client initialized');
    }
    
    setupEventListeners() {
        // Connection controls
        document.getElementById('connectBtn').addEventListener('click', () => this.connect());
        
        // Recording controls
        document.getElementById('recordBtn').addEventListener('click', () => this.startRecording());
        document.getElementById('pauseBtn').addEventListener('click', () => this.pauseRecording());
        document.getElementById('stopBtn').addEventListener('click', () => this.stopRecording());
        
        // Audio controls with real-time updates
        this.setupAudioControls();
        
        // Log controls
        document.getElementById('clearLogBtn').addEventListener('click', () => this.clearLog());
        
        // Window events
        window.addEventListener('beforeunload', () => this.disconnect());
        window.addEventListener('online', () => this.handleNetworkChange(true));
        window.addEventListener('offline', () => this.handleNetworkChange(false));
    }
    
    setupAudioControls() {
        const controls = [
            { id: 'inputGain', valueId: 'inputGainValue', suffix: '%', callback: (value) => this.setInputGain(value / 100) },
            { id: 'monitorVolume', valueId: 'monitorVolumeValue', suffix: '%', callback: (value) => this.setMonitorVolume(value / 100) },
            { id: 'noiseGate', valueId: 'noiseGateValue', suffix: ' dB', callback: (value) => this.setNoiseGate(value) },
            { id: 'lowCut', valueId: 'lowCutValue', suffix: ' Hz', callback: (value) => this.setLowCut(value) }
        ];
        
        controls.forEach(control => {
            const slider = document.getElementById(control.id);
            const valueDisplay = document.getElementById(control.valueId);
            
            slider.addEventListener('input', (e) => {
                const value = parseFloat(e.target.value);
                valueDisplay.textContent = value + control.suffix;
                control.callback(value);
            });
        });
    }
    
    async connect() {
        if (this.isConnected) {
            this.log('warning', 'Already connected to FL Studio');
            return;
        }
        
        const serverAddress = document.getElementById('serverAddress').value.trim();
        this.roomId = document.getElementById('roomId').value.trim();
        this.userName = document.getElementById('userName').value.trim() || 'Vocalist';
        
        if (!serverAddress || !this.roomId) {
            this.log('error', 'Please enter server address and room ID');
            return;
        }
        
        try {
            this.log('info', `Connecting to FL Studio server: ${serverAddress}`);
            this.updateConnectionStatus('Connecting...', 'connecting');
            
            // Determine WebSocket URL
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = serverAddress.startsWith('ws://') || serverAddress.startsWith('wss://') ? 
                         serverAddress : `${protocol}//${serverAddress}`;
            
            // Create WebSocket connection
            this.websocket = new WebSocket(wsUrl);
            
            this.websocket.onopen = () => this.handleConnectionOpen();
            this.websocket.onmessage = (event) => this.handleMessage(event);
            this.websocket.onclose = (event) => this.handleConnectionClose(event);
            this.websocket.onerror = (error) => this.handleConnectionError(error);
            
            // Connection timeout
            setTimeout(() => {
                if (!this.isConnected) {
                    this.log('error', 'Connection timeout');
                    this.disconnect();
                }
            }, 10000);
            
        } catch (error) {
            this.log('error', `Connection failed: ${error.message}`);
            this.updateConnectionStatus('Connection failed', 'disconnected');
        }
    }
    
    handleConnectionOpen() {
        this.isConnected = true;
        this.userId = `vocalist-${Date.now()}-${Math.random().toString(36).substr(2, 5)}`;
        
        this.log('success', 'Connected to FL Studio server');
        this.updateConnectionStatus('Connected to FL Studio', 'connected');
        
        // Send join room message
        this.sendMessage({
            type: 'join',
            roomId: this.roomId,
            userId: this.userId,
            userName: this.userName,
            userType: 'producer',
            capabilities: {
                audio: true,
                sampleRate: 48000,
                channels: 1,
                bufferSize: 512
            }
        });
        
        // Update UI
        document.getElementById('connectBtn').disabled = true;
        document.getElementById('recordBtn').disabled = false;
        document.getElementById('recordingPanel').style.display = 'block';
        document.getElementById('audioControls').style.display = 'block';
        document.getElementById('sessionInfo').style.display = 'block';
        
        // Start statistics updates
        this.startStatsUpdates();
    }
    
    handleConnectionClose(event) {
        this.isConnected = false;
        this.stopRecording();
        
        const reason = event.code === 1000 ? 'Normal closure' : 
                      event.code === 1006 ? 'Connection lost' : 
                      `Connection closed (${event.code})`;
        
        this.log('warning', `Disconnected: ${reason}`);
        this.updateConnectionStatus('Disconnected', 'disconnected');
        
        // Reset UI
        this.resetUI();
        
        // Attempt reconnection if unexpected
        if (event.code !== 1000 && event.code !== 1001) {
            this.log('info', 'Attempting reconnection in 5 seconds...');
            setTimeout(() => {
                if (!this.isConnected) {
                    this.connect();
                }
            }, 5000);
        }
    }
    
    handleConnectionError(error) {
        this.log('error', `WebSocket error: ${error.message || 'Unknown error'}`);
        this.updateConnectionStatus('Connection error', 'disconnected');
    }
    
    handleMessage(event) {
        try {
            const message = JSON.parse(event.data);
            
            switch (message.type) {
                case 'welcome':
                    this.log('success', `Joined room: ${message.roomId}`);
                    this.updateSessionInfo(message);
                    break;
                    
                case 'userJoined':
                    this.log('info', `${message.userName} joined the session`);
                    break;
                    
                case 'userLeft':
                    this.log('info', `${message.userName} left the session`);
                    break;
                    
                case 'audioData':
                    this.handleIncomingAudio(message.data);
                    break;
                    
                case 'stats':
                    this.updateNetworkStats(message.data);
                    break;
                    
                case 'error':
                    this.log('error', `Server error: ${message.message}`);
                    break;
                    
                default:
                    this.log('warning', `Unknown message type: ${message.type}`);
            }
        } catch (error) {
            this.log('error', `Failed to parse message: ${error.message}`);
        }
    }
    
    async startRecording() {
        if (this.isRecording) return;
        
        try {
            this.log('info', 'Starting audio recording...');
            
            // Request microphone access
            this.mediaStream = await navigator.mediaDevices.getUserMedia({
                audio: {
                    echoCancellation: false,
                    noiseSuppression: false,
                    autoGainControl: false,
                    sampleRate: 48000,
                    channelCount: 1
                }
            });
            
            // Create audio context
            this.audioContext = new AudioContext({ sampleRate: 48000 });
            
            // Load and create audio worklet
            await this.audioContext.audioWorklet.addModule('audio-processor.js');
            
            const source = this.audioContext.createMediaStreamSource(this.mediaStream);
            this.workletNode = new AudioWorkletNode(this.audioContext, 'fl-studio-audio-processor');
            
            // Connect audio processing chain
            source.connect(this.workletNode);
            
            // Handle processed audio data
            this.workletNode.port.onmessage = (event) => {
                this.handleProcessedAudio(event.data);
            };
            
            this.isRecording = true;
            this.isPaused = false;
            
            this.log('success', 'Recording started - streaming to FL Studio');
            this.updateConnectionStatus('Recording and streaming to FL Studio', 'connected');
            
            // Update UI
            document.getElementById('recordBtn').disabled = true;
            document.getElementById('pauseBtn').disabled = false;
            document.getElementById('stopBtn').disabled = false;
            document.getElementById('recordBtn').classList.add('recording');
            
        } catch (error) {
            this.log('error', `Failed to start recording: ${error.message}`);
            
            if (error.name === 'NotAllowedError') {
                this.log('error', 'Microphone access denied. Please allow microphone access and try again.');
            } else if (error.name === 'NotFoundError') {
                this.log('error', 'No microphone found. Please connect a microphone and try again.');
            }
        }
    }
    
    pauseRecording() {
        if (!this.isRecording) return;
        
        this.isPaused = !this.isPaused;
        
        if (this.isPaused) {
            this.log('warning', 'Recording paused');
            document.getElementById('pauseBtn').textContent = '▶️ Resume';
        } else {
            this.log('info', 'Recording resumed');
            document.getElementById('pauseBtn').textContent = '⏸️ Pause';
        }
    }
    
    stopRecording() {
        if (!this.isRecording) return;
        
        this.log('info', 'Stopping recording...');
        
        // Stop media stream
        if (this.mediaStream) {
            this.mediaStream.getTracks().forEach(track => track.stop());
            this.mediaStream = null;
        }
        
        // Close audio context
        if (this.audioContext) {
            this.audioContext.close();
            this.audioContext = null;
        }
        
        this.isRecording = false;
        this.isPaused = false;
        
        this.log('success', 'Recording stopped');
        this.updateConnectionStatus('Connected to FL Studio', 'connected');
        
        // Update UI
        document.getElementById('recordBtn').disabled = false;
        document.getElementById('pauseBtn').disabled = true;
        document.getElementById('stopBtn').disabled = true;
        document.getElementById('recordBtn').classList.remove('recording');
        document.getElementById('pauseBtn').textContent = '⏸️ Pause';
    }
    
    handleProcessedAudio(data) {
        if (!this.isConnected || !this.isRecording || this.isPaused) return;
        
        switch (data.type) {
            case 'audioData':
                this.sendAudioData(data);
                this.updateInputMeter(data.rmsLevel, data.peakLevel);
                break;
                
            case 'audioLevel':
                this.updateInputMeter(data.rms, data.peak);
                break;
        }
    }
    
    sendAudioData(audioData) {
        if (!this.websocket || this.websocket.readyState !== WebSocket.OPEN) return;
        
        const packet = {
            type: 'audioData',
            data: {
                timestamp: audioData.timestamp,
                sequenceId: this.stats.packetsSent++,
                channels: 1,
                sampleRate: 48,
                numSamples: audioData.data.length,
                audioData: audioData.data
            },
            userId: this.userId,
            roomId: this.roomId
        };
        
        try {
            const message = JSON.stringify(packet);
            this.websocket.send(message);
            this.stats.bytesSent += message.length;
        } catch (error) {
            this.log('error', `Failed to send audio: ${error.message}`);
        }
    }
    
    handleIncomingAudio(audioPacket) {
        // Handle audio from other users (for monitoring)
        this.stats.packetsReceived++;
        this.stats.bytesReceived += audioPacket.audioData.length * 4; // float32
        
        // Update output meter with received audio
        if (audioPacket.audioData && audioPacket.audioData.length > 0) {
            const samples = new Float32Array(audioPacket.audioData);
            const rms = this.calculateRMS(samples);
            const peak = Math.max(...samples.map(Math.abs));
            this.updateOutputMeter(rms, peak);
        }
    }
    
    calculateRMS(samples) {
        let sum = 0;
        for (let i = 0; i < samples.length; i++) {
            sum += samples[i] * samples[i];
        }
        return Math.sqrt(sum / samples.length);
    }
    
    updateInputMeter(rms, peak) {
        if (this.inputMeter) {
            this.inputMeter.update(rms, peak);
            
            const rmsDB = rms > 0 ? 20 * Math.log10(rms) : -120;
            document.getElementById('inputLevel').textContent = 
                rmsDB > -120 ? `${rmsDB.toFixed(1)} dB` : '-∞ dB';
        }
    }
    
    updateOutputMeter(rms, peak) {
        if (this.outputMeter) {
            this.outputMeter.update(rms * this.monitorVolume, peak * this.monitorVolume);
            
            const rmsDB = rms > 0 ? 20 * Math.log10(rms * this.monitorVolume) : -120;
            document.getElementById('outputLevel').textContent = 
                rmsDB > -120 ? `${rmsDB.toFixed(1)} dB` : '-∞ dB';
        }
    }
    
    // Audio parameter setters
    setInputGain(gain) {
        this.inputGain = Math.max(0, Math.min(2.0, gain));
        if (this.workletNode) {
            this.workletNode.port.postMessage({
                type: 'setInputGain',
                value: this.inputGain
            });
        }
    }
    
    setMonitorVolume(volume) {
        this.monitorVolume = Math.max(0, Math.min(1.0, volume));
    }
    
    setNoiseGate(threshold) {
        this.noiseGateThreshold = Math.max(-60, Math.min(-20, threshold));
        if (this.workletNode) {
            this.workletNode.port.postMessage({
                type: 'setNoiseGate',
                value: this.noiseGateThreshold
            });
        }
    }
    
    setLowCut(frequency) {
        this.lowCutFreq = Math.max(20, Math.min(200, frequency));
        if (this.workletNode) {
            this.workletNode.port.postMessage({
                type: 'setLowCut',
                value: this.lowCutFreq
            });
        }
    }
    
    disconnect() {
        if (!this.isConnected) return;
        
        this.log('info', 'Disconnecting from FL Studio...');
        
        this.stopRecording();
        
        if (this.websocket) {
            this.websocket.close(1000, 'User initiated disconnect');
            this.websocket = null;
        }
        
        this.isConnected = false;
        this.resetUI();
        
        this.log('success', 'Disconnected from FL Studio');
    }
    
    resetUI() {
        document.getElementById('connectBtn').disabled = false;
        document.getElementById('recordBtn').disabled = true;
        document.getElementById('pauseBtn').disabled = true;
        document.getElementById('stopBtn').disabled = true;
        document.getElementById('recordingPanel').style.display = 'none';
        document.getElementById('audioControls').style.display = 'none';
        document.getElementById('sessionInfo').style.display = 'none';
    }
    
    updateConnectionStatus(message, status) {
        const statusPanel = document.getElementById('connectionStatus');
        const statusText = document.getElementById('statusText');
        
        statusText.textContent = message;
        statusPanel.className = `status-panel ${status}`;
    }
    
    updateSessionInfo(info) {
        if (info.sampleRate) {
            document.getElementById('sampleRate').textContent = `${info.sampleRate} Hz`;
        }
        if (info.userCount !== undefined) {
            document.getElementById('userCount').textContent = info.userCount;
        }
    }
    
    updateNetworkStats(stats) {
        if (stats.latency !== undefined) {
            document.getElementById('latency').textContent = `${stats.latency.toFixed(1)} ms`;
        }
        if (stats.quality !== undefined) {
            document.getElementById('networkQuality').textContent = stats.quality;
        }
    }
    
    startStatsUpdates() {
        setInterval(() => {
            if (this.isConnected && this.websocket.readyState === WebSocket.OPEN) {
                // Calculate and display statistics
                const runtime = (Date.now() - this.stats.startTime) / 1000;
                const sendRate = this.stats.bytesSent / runtime;
                const receiveRate = this.stats.bytesReceived / runtime;
                
                // Send stats request
                this.sendMessage({
                    type: 'getStats',
                    userId: this.userId
                });
            }
        }, 2000);
    }
    
    sendMessage(message) {
        if (this.websocket && this.websocket.readyState === WebSocket.OPEN) {
            try {
                this.websocket.send(JSON.stringify(message));
            } catch (error) {
                this.log('error', `Failed to send message: ${error.message}`);
            }
        }
    }
    
    handleNetworkChange(online) {
        if (online) {
            this.log('info', 'Network connection restored');
            if (!this.isConnected) {
                setTimeout(() => this.connect(), 1000);
            }
        } else {
            this.log('warning', 'Network connection lost');
        }
    }
    
    log(level, message) {
        const timestamp = new Date().toLocaleTimeString();
        const logContent = document.getElementById('logContent');
        
        const logEntry = document.createElement('div');
        logEntry.className = 'log-entry';
        logEntry.innerHTML = `
            <span class="log-timestamp">${timestamp}</span>
            <span class="log-level-${level}">[${level.toUpperCase()}]</span>
            ${message}
        `;
        
        logContent.appendChild(logEntry);
        logContent.scrollTop = logContent.scrollHeight;
        
        // Console logging for debugging
        console.log(`[FL Vocal Client ${level.toUpperCase()}] ${message}`);
    }
    
    clearLog() {
        document.getElementById('logContent').innerHTML = '';
        this.log('info', 'Log cleared');
    }
}

// Initialize the FL Studio Vocal Client when the page loads
document.addEventListener('DOMContentLoaded', () => {
    window.flVocalClient = new FLStudioVocalClient();
});