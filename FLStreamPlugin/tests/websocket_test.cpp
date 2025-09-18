#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "../Source/WebSocketServer.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace juce;

//==============================================================================
/** Simple command-line WebSocket test */
class WebSocketTest {
public:
    static void runServerTest(int port = 9001) {
        std::cout << "=== FL Stream WebSocket Server Test ===" << std::endl;
        std::cout << "Starting server on port " << port << "..." << std::endl;
        
        // Create server
        FLStreamWebSocketServer server;
        
        // Set up logging
        server.onLogMessage = [](const String& message) {
            std::cout << "[SERVER] " << message.toStdString() << std::endl;
        };
        
        server.onUserJoined = [](const String& userId, const String& roomId) {
            std::cout << "[SERVER] User joined: " << userId.toStdString() 
                      << " in room: " << roomId.toStdString() << std::endl;
        };
        
        server.onUserLeft = [](const String& userId, const String& roomId) {
            std::cout << "[SERVER] User left: " << userId.toStdString() 
                      << " from room: " << roomId.toStdString() << std::endl;
        };
        
        // Start server
        bool started = server.startServer(port);
        if (!started) {
            std::cout << "❌ Failed to start server on port " << port << std::endl;
            return;
        }
        
        std::cout << "✅ Server started successfully on port " << port << std::endl;
        std::cout << "🌐 Web interface available at: http://localhost:" << port << std::endl;
        std::cout << "Press Enter to stop server..." << std::endl;
        
        // Wait for user input
        std::cin.get();
        
        std::cout << "Stopping server..." << std::endl;
        server.stopServer();
        std::cout << "✅ Server stopped" << std::endl;
    }
    
    static void runClientTest(const String& serverAddress = "localhost", int port = 9001) {
        std::cout << "=== FL Stream WebSocket Client Test ===" << std::endl;
        std::cout << "Connecting to " << serverAddress.toStdString() 
                  << ":" << port << "..." << std::endl;
        
        // Create client
        FLStreamWebSocketClient client;
        
        // Set up logging
        client.onLogMessage = [](const String& message) {
            std::cout << "[CLIENT] " << message.toStdString() << std::endl;
        };
        
        // Note: Client callbacks are handled internally
        
        // Attempt connection
        bool connected = client.connectToServer(serverAddress, port, "TEST-ROOM");
        
        if (connected) {
            std::cout << "✅ Connected successfully!" << std::endl;
            std::cout << "Press Enter to disconnect..." << std::endl;
            std::cin.get();
        } else {
            std::cout << "❌ Failed to connect" << std::endl;
        }
        
        std::cout << "Disconnecting..." << std::endl;
        client.disconnect();
        std::cout << "✅ Disconnected" << std::endl;
    }
    
    static void runWebBrowserTest(int port = 9001) {
        std::cout << "=== Web Browser Connection Test ===" << std::endl;
        
        // Start server
        FLStreamWebSocketServer server;
        server.onLogMessage = [](const String& message) {
            std::cout << "[SERVER] " << message.toStdString() << std::endl;
        };
        
        bool started = server.startServer(port);
        if (!started) {
            std::cout << "❌ Failed to start server" << std::endl;
            return;
        }
        
        std::cout << "✅ Server started on port " << port << std::endl;
        std::cout << "🌐 Opening browser to test WebSocket connection..." << std::endl;
        
        // Open browser
        String url = "http://localhost:" + String(port);
        URL(url).launchInDefaultBrowser();
        
        std::cout << "Browser should open to: " << url.toStdString() << std::endl;
        std::cout << "Check browser console for WebSocket connection status" << std::endl;
        std::cout << "Press Enter when done testing..." << std::endl;
        
        std::cin.get();
        
        server.stopServer();
        std::cout << "✅ Test complete" << std::endl;
    }
};

//==============================================================================
int main(int argc, char* argv[]) {
    // Initialize JUCE
    MessageManager::getInstance();
    
    std::cout << "FL Stream WebSocket Connection Tester" << std::endl;
    std::cout << "====================================" << std::endl;
    
    if (argc > 1) {
        String arg(argv[1]);
        
        if (arg == "--server" || arg == "-s") {
            int port = 9001;
            if (argc > 2) port = String(argv[2]).getIntValue();
            WebSocketTest::runServerTest(port);
        }
        else if (arg == "--client" || arg == "-c") {
            String address = "localhost";
            int port = 9001;
            if (argc > 2) address = String(argv[2]);
            if (argc > 3) port = String(argv[3]).getIntValue();
            WebSocketTest::runClientTest(address, port);
        }
        else if (arg == "--web" || arg == "-w") {
            int port = 9001;
            if (argc > 2) port = String(argv[2]).getIntValue();
            WebSocketTest::runWebBrowserTest(port);
        }
        else {
            std::cout << "Unknown option: " << arg.toStdString() << std::endl;
        }
    } else {
        std::cout << "Usage:" << std::endl;
        std::cout << "  " << argv[0] << " --server [port]     # Start WebSocket server" << std::endl;
        std::cout << "  " << argv[0] << " --client [host] [port] # Connect as client" << std::endl;
        std::cout << "  " << argv[0] << " --web [port]        # Test with web browser" << std::endl;
        std::cout << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  " << argv[0] << " --server 9001" << std::endl;
        std::cout << "  " << argv[0] << " --client localhost 9001" << std::endl;
        std::cout << "  " << argv[0] << " --web 9001" << std::endl;
        std::cout << std::endl;
        std::cout << "Running default server test..." << std::endl;
        WebSocketTest::runServerTest();
    }
    
    MessageManager::deleteInstance();
    return 0;
}