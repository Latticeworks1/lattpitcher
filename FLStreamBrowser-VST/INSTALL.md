# FL Stream Browser VST - Installation Guide

## Quick Installation (macOS)

### Option 1: Use Installation Script (Recommended)

Run the installation script to automatically install all plugin formats:

```bash
chmod +x install.sh
./install.sh
```

### Option 2: Manual Installation

#### VST3 Plugin (FL Studio, Ableton Live, etc.)
```bash
cp -r "FLStreamPlugin_artefacts/VST3/FL Stream Plugin.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

#### AU Plugin (Logic Pro, GarageBand, etc.)
```bash
cp -r "FLStreamPlugin_artefacts/AU/FL Stream Plugin.component" ~/Library/Audio/Plug-Ins/Components/
```

#### Standalone Application
```bash
cp -r "FLStreamPlugin_artefacts/Standalone/FL Stream Plugin.app" /Applications/
```

## Verification

### Test the Installation

1. **VST3**: Open FL Studio → Add → Plugin Browser → "FL Stream Plugin"
2. **AU**: Open Logic Pro → Channel Strip → Instrument/Audio FX → "FL Stream Plugin" 
3. **Standalone**: Launch from Applications folder or Spotlight search

### Expected Behavior

- Plugin window opens showing web browser with navigation controls
- Address bar loads `https://voice.latticeworks-ai.com` by default
- Browser controls (back, forward, home, go) are functional
- Console output shows: `"FL Stream: General Web Browser initialized"`

## Post-Installation Setup

### DAW Configuration

1. **Add to Track**: Insert FL Stream Plugin on an audio track
2. **Automation**: Map the "Talking" parameter to your controller for push-to-talk
3. **Routing**: Connect microphone input to the plugin track

### Voice Chat Setup

1. Click **Home** button to navigate to FL Stream voice interface
2. Interface auto-joins "my_room" after 1 second
3. Use push-to-talk button or automate "Talking" parameter
4. Monitor connection status in the web interface

## Uninstallation

Remove plugin files:

```bash
# VST3
rm -rf ~/Library/Audio/Plug-Ins/VST3/FL\ Stream\ Plugin.vst3

# AU  
rm -rf ~/Library/Audio/Plug-Ins/Components/FL\ Stream\ Plugin.component

# Standalone
rm -rf /Applications/FL\ Stream\ Plugin.app
```

## Troubleshooting

### Plugin Not Appearing in DAW

1. **Check Installation Path**:
   ```bash
   ls ~/Library/Audio/Plug-Ins/VST3/
   ls ~/Library/Audio/Plug-Ins/Components/
   ```

2. **Restart DAW**: Completely quit and reopen your DAW

3. **Rescan Plugins**: Use your DAW's plugin manager to rescan

4. **Check Permissions**: 
   ```bash
   chmod -R 755 ~/Library/Audio/Plug-Ins/VST3/FL\ Stream\ Plugin.vst3
   chmod -R 755 ~/Library/Audio/Plug-Ins/Components/FL\ Stream\ Plugin.component
   ```

### Voice Chat Connection Issues

1. **Check Internet**: Verify connection to `voice.latticeworks-ai.com`
2. **Firewall**: Ensure WebSocket connections (port 443) are allowed
3. **Browser Console**: Check for JavaScript errors in the web interface

### Performance Issues

1. **Buffer Size**: Increase audio buffer size in DAW preferences
2. **CPU Usage**: Close unnecessary browser tabs/applications  
3. **Network**: Use wired internet connection for stability

## Advanced Configuration

### Custom Server Configuration

The plugin connects to `voice.latticeworks-ai.com` by default. This is configured in:
- `Source/FLStreamEditor.cpp:650` - Default home URL
- `Source/FLStreamProcessor.cpp:5` - Colyseus server address

### Building Custom Version

See main README.md for build instructions if you need to modify the source code.

## Support

- **GitHub Issues**: Report installation problems
- **Documentation**: Check README.md for technical details  
- **DAW Compatibility**: Test with your specific DAW version

---

✅ **Installation Complete** - Your FL Stream Browser VST is ready to use!