import serial
import time
import threading
import sys

# Configure serial port
ser = None
running = True

# Define query patterns and their responses
QUERY_PATTERNS = {
    b'\x01\x02\x03\xFF': b'Query 1 response\n',
    b'\x02\x02\x03\xFF': b'Query 2 response\n'
}

def heartbeat():
    """Show that the script is still alive"""
    dots = 0
    while running:
        if ser and ser.is_open:
            sys.stdout.write(f"\rListening for queries{' ' * dots}")
            sys.stdout.flush()
            dots = (dots + 1) % 4
        time.sleep(0.5)

def handle_received_data(data):
    """Process received data and send appropriate responses"""
    for pattern, response in QUERY_PATTERNS.items():
        if pattern in data:
            timestamp = time.strftime('%H:%M:%S')
            print(f"\n[{timestamp}] ✅ Matched pattern: {pattern.hex()}")
            if ser and ser.is_open:
                ser.write(response)
                print(f"[{timestamp}] 📤 Sent response: {response.decode('utf-8').strip()}")
                return True
    return False

def read_serial():
    """Function to continuously read and respond to incoming serial data"""
    global ser, running
    buffer = bytearray()
    
    while running:
        try:
            if ser and ser.is_open and ser.in_waiting > 0:
                # Clear the heartbeat line
                print()
                
                # Read all available data
                data = ser.read(ser.in_waiting)
                buffer.extend(data)
                
                timestamp = time.strftime('%H:%M:%S')
                print(f"[{timestamp}] 📥 Received hex: {data.hex()}")
                
                # Check for patterns in buffer
                if handle_received_data(buffer):
                    # Clear buffer after matching
                    buffer.clear()
                
                # Optional: Clear buffer if too large
                if len(buffer) > 1024:
                    buffer.clear()
                    
            time.sleep(0.05)
            
        except serial.SerialException as e:
            print(f"\n❌ Read error: {e}")
            time.sleep(0.5)

def main():
    global ser, running
    
    print("🔌 Serial Query Responder")
    print("=" * 50)
    print("Listening for query patterns:")
    for pattern, response in QUERY_PATTERNS.items():
        print(f"  Pattern: {pattern.hex()} ➜ Response: {response.decode('utf-8').strip()}")
    print("=" * 50)
    print("Press Ctrl+C to stop\n")
    
    # Start heartbeat thread
    heartbeat_thread = threading.Thread(target=heartbeat)
    heartbeat_thread.daemon = True
    heartbeat_thread.start()
    
    # Start reader thread
    reader_thread = threading.Thread(target=read_serial)
    reader_thread.daemon = True
    reader_thread.start()
    
    try:
        while True:
            try:
                # Try to connect/reconnect if needed
                if ser is None or not ser.is_open:
                    ser = serial.Serial(
                        port='/dev/ttyUSB0',
                        baudrate=9600,
                        timeout=1
                    )
                    print(f"\n✅ Connected at {time.strftime('%H:%M:%S')}")
                
                time.sleep(1)
                
            except serial.SerialException as e:
                print(f"\n❌ Serial error: {e} - reconnecting in 2 seconds...")
                if ser and ser.is_open:
                    ser.close()
                ser = None
                time.sleep(2)
                
    except KeyboardInterrupt:
        print("\n\n👋 Stopped by user")
    finally:
        running = False
        if ser and ser.is_open:
            ser.close()
            print("✅ Serial port closed")

if __name__ == "__main__":
    main()