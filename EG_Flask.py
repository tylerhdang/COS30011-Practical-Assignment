import serial
import time
import json
import pymysql
from flask import Flask, render_template, request, jsonify
from datetime import datetime, timedelta

app = Flask(__name__)

# Configuration
DEVICE = '/dev/ttyACM0'  # Arduino serial port
BAUD_RATE = 9600
DB_HOST = 'localhost'
DB_USER = 'pi'
DB_PASSWORD = ''
DB_NAME = 'enviroguardian_db'

# Dictionary of device status
status = {
    'mode': 'NORMAL',
    'control': 'AUTO',
    'red_led': False,
    'yellow_led': False,
    'green_led': False,
    'servo_position': 0,
    'temperature': 0.0,
    'humidity': 0.0,
    'thresholds': {
        'goodTempMin': 22.0,
        'goodTempMax': 27.0,
        'goodHumMin': 40.0,
        'goodHumMax': 60.0,
        'acceptableTempMin': 20.0,
        'acceptableTempMax': 30.0,
        'acceptableHumMin': 30.0,
        'acceptableHumMax': 70.0
    }
}

# Connect to Arduino
try:
    ser = serial.Serial(DEVICE, BAUD_RATE, timeout=1)
    ser.flush()
    connected = True
    print(f"Connected to Arduino on {DEVICE}")
    
    # Request current thresholds
    time.sleep(2)  # Wait for Arduino to be ready
    ser.write(b'G')  # Get current thresholds
    time.sleep(0.5)
    
except Exception as e:
    connected = False
    print(f"Failed to connect to Arduino: {e}")

# Connect to database
def get_db_connection():
    try:
        conn = pymysql.connect(
            host=DB_HOST,
            user=DB_USER,
            password=DB_PASSWORD,
            database=DB_NAME
        )
        return conn
    except Exception as e:
        print(f"Database connection error: {e}")
        return None

def read_serial_data():
     if connected and ser.in_waiting > 0:
        line = ser.readline().decode('utf-8').strip()
        
        # Skip threshold updates if we just set them
        if "THRESHOLDS|" in line and not line.startswith("THRESHOLDS|"):
            return  # Ignore automatic threshold reports
        """Read and parse data from Arduino's serial output"""
        if connected and ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8').strip()
                print(f"Received from Arduino: {line}")  # Debug output
                
                # Check if this is a thresholds response
                if line.startswith("THRESHOLDS|"):
                    parts = line.split("|")
                    if len(parts) == 9:  # THRESHOLDS + 8 values
                        status['thresholds']['goodTempMin'] = float(parts[1])
                        status['thresholds']['goodTempMax'] = float(parts[2])
                        status['thresholds']['goodHumMin'] = float(parts[3])
                        status['thresholds']['goodHumMax'] = float(parts[4])
                        status['thresholds']['acceptableTempMin'] = float(parts[5])
                        status['thresholds']['acceptableTempMax'] = float(parts[6])
                        status['thresholds']['acceptableHumMin'] = float(parts[7])
                        status['thresholds']['acceptableHumMax'] = float(parts[8])
                    return
                
                # Parse temperature if available
                if "FinalTemp:" in line:
                    temp_start = line.find("FinalTemp:") + len("FinalTemp:")
                    temp_end = line.find("°C", temp_start)
                    temp_str = line[temp_start:temp_end].strip()
                    status['temperature'] = float(temp_str)
                
                # Parse humidity if available
                if "FinalHum:" in line:
                    hum_start = line.find("FinalHum:") + len("FinalHum:")
                    hum_end = line.find("%", hum_start)
                    hum_str = line[hum_start:hum_end].strip()
                    status['humidity'] = float(hum_str)
                
                # Parse mode if available
                if "Mode:" in line:
                    mode_start = line.find("Mode:") + len("Mode:")
                    mode_end = line.find("|", mode_start)
                    if mode_end == -1:
                        mode_end = len(line)
                    mode_str = line[mode_start:mode_end].strip()
                    status['mode'] = mode_str
                
                # Parse control mode if available
                if "Control:" in line:
                    control_start = line.find("Control:") + len("Control:")
                    control_end = line.find("|", control_start)
                    if control_end == -1:
                        control_end = len(line)
                    control_str = line[control_start:control_end].strip()
                    status['control'] = control_str
                
            except (ValueError, IndexError, UnicodeDecodeError) as e:
                print(f"Error parsing serial data: {e}")
            except Exception as e:
                print(f"Unexpected error reading serial data: {e}")

@app.route('/')
def index():
    # Get latest thresholds from Arduino
    if connected:
        ser.write(b'G')  # Get current thresholds
        time.sleep(0.5)  # Wait for response
        read_serial_data()  # Process response
    
    return render_template('index.html', status=status, connected=connected)

@app.route('/api/status')
def get_status():
    if connected:
        read_serial_data()  # Update status with latest readings
    return jsonify(status)

@app.route('/api/control/mode/<mode>')
def set_mode(mode):
    if not connected:
        return jsonify({'success': False, 'message': 'Arduino not connected'})

    if mode == 'normal':
        ser.write(b'M1')
        status['mode'] = 'NORMAL'
    elif mode == 'offset':
        ser.write(b'M2')
        status['mode'] = 'OFFSET'
    elif mode == 'override':
        ser.write(b'M3')
        status['mode'] = 'OVERRIDE'
    else:
        return jsonify({'success': False, 'message': 'Invalid mode'})
    
    return jsonify({'success': True, 'mode': status['mode']})

@app.route('/api/control/auto/<state>')
def set_auto(state):
    if not connected:
        return jsonify({'success': False, 'message': 'Arduino not connected'})

    if state == 'on':
        ser.write(b'A')
        status['control'] = 'AUTO'
    elif state == 'off':
        ser.write(b'R')
        status['control'] = 'REMOTE'
    else:
        return jsonify({'success': False, 'message': 'Invalid state'})
    
    return jsonify({'success': True, 'control': status['control']})

@app.route('/api/control/led/<led>/<state>')
def set_led(led, state):
    if not connected:
        return jsonify({'success': False, 'message': 'Arduino not connected'})

    # Map LED name to command
    led_cmds = {
        'red': ('1', '2'),
        'yellow': ('3', '4'),
        'green': ('5', '6')
    }
    
    if led not in led_cmds:
        return jsonify({'success': False, 'message': 'Invalid LED'})
    
    if state == 'on':
        ser.write(led_cmds[led][0].encode())
        status[f'{led}_led'] = True
        status['control'] = 'REMOTE'  # Switching to remote mode
    elif state == 'off':
        ser.write(led_cmds[led][1].encode())
        status[f'{led}_led'] = False
        status['control'] = 'REMOTE'  # Switching to remote mode
    else:
        return jsonify({'success': False, 'message': 'Invalid state'})
    
    return jsonify({'success': True, f'{led}_led': status[f'{led}_led']})

@app.route('/api/control/buzzer')
def trigger_buzzer():
    if not connected:
        return jsonify({'success': False, 'message': 'Arduino not connected'})
    
    ser.write(b'B')
    status['control'] = 'REMOTE'  # Switching to remote mode
    
    return jsonify({'success': True, 'message': 'Buzzer triggered'})

@app.route('/api/control/servo/<position>')
def set_servo(position):
    if not connected:
        return jsonify({'success': False, 'message': 'Arduino not connected'})
    
    try:
        pos = int(position)
        if 0 <= pos <= 180:
            # Format servo position as 3 digits (e.g., 045)
            formatted_pos = f"{pos:03d}"
            ser.write(f"S{formatted_pos}".encode())
            status['servo_position'] = pos
            status['control'] = 'REMOTE'  # Switching to remote mode
            return jsonify({'success': True, 'servo_position': pos})
        else:
            return jsonify({'success': False, 'message': 'Position must be between 0 and 180'})
    except ValueError:
        return jsonify({'success': False, 'message': 'Invalid position'})

@app.route('/api/thresholds/update', methods=['POST'])
def update_thresholds():
    if not connected:
        return jsonify({'success': False, 'message': 'Arduino not connected'})
    
    try:
        data = request.json
        
        # Validate data
        required_fields = [
            'goodTempMin', 'goodTempMax', 'goodHumMin', 'goodHumMax',
            'acceptableTempMin', 'acceptableTempMax', 'acceptableHumMin', 'acceptableHumMax'
        ]
        
        for field in required_fields:
            if field not in data:
                return jsonify({'success': False, 'message': f'Missing field: {field}'})
        
        # Update thresholds in Arduino
        # Temperature thresholds
        ser.write(f"T1{data['goodTempMin']}\n".encode())
        time.sleep(0.1)
        ser.write(f"T2{data['goodTempMax']}\n".encode())
        time.sleep(0.1)
        ser.write(f"T3{data['acceptableTempMin']}\n".encode())
        time.sleep(0.1)
        ser.write(f"T4{data['acceptableTempMax']}\n".encode())
        time.sleep(0.1)
        
        # Humidity thresholds
        ser.write(f"H1{data['goodHumMin']}\n".encode())
        time.sleep(0.1)
        ser.write(f"H2{data['goodHumMax']}\n".encode())
        time.sleep(0.1)
        ser.write(f"H3{data['acceptableHumMin']}\n".encode())
        time.sleep(0.1)
        ser.write(f"H4{data['acceptableHumMax']}\n".encode())
        time.sleep(0.1)
        
        # Update local status
        status['thresholds'] = {
            'goodTempMin': float(data['goodTempMin']),
            'goodTempMax': float(data['goodTempMax']),
            'goodHumMin': float(data['goodHumMin']),
            'goodHumMax': float(data['goodHumMax']),
            'acceptableTempMin': float(data['acceptableTempMin']),
            'acceptableTempMax': float(data['acceptableTempMax']),
            'acceptableHumMin': float(data['acceptableHumMin']),
            'acceptableHumMax': float(data['acceptableHumMax'])
        }
        
        return jsonify({'success': True, 'message': 'Thresholds updated'})
            
    except Exception as e:
        return jsonify({'success': False, 'message': f'Error updating thresholds: {str(e)}'})

@app.route('/api/data/history')
def get_history_data():
    """Fetch historical data for chart visualization"""
    conn = get_db_connection()
    if not conn:
        return jsonify({'error': 'Database connection failed'})
    
    try:
        cursor = conn.cursor()
        
        # Get data from the last 24 hours
        query = """
        SELECT timestamp, final_temp, final_humidity 
        FROM sensor_readings 
        WHERE timestamp >= %s 
        ORDER BY timestamp ASC
        """
        time_threshold = datetime.now() - timedelta(hours=24)
        cursor.execute(query, (time_threshold.strftime('%Y-%m-%d %H:%M:%S'),))  # ✅ Fixed parenthesis
        
        data = cursor.fetchall()
        
        # Format for Chart.js
        timestamps = [row[0].strftime('%Y-%m-%d %H:%M:%S') for row in data]
        temperatures = [float(row[1]) for row in data]
        humidities = [float(row[2]) for row in data]
        
        return jsonify({
            'timestamps': timestamps,
            'temperatures': temperatures,
            'humidities': humidities
        })
        
    except Exception as e:
        return jsonify({'error': str(e)})
    finally:
        conn.close()

@app.route('/api/data/recent')
def get_recent_data():
    """Fetch last 5 entries for the table display"""
    conn = get_db_connection()
    if not conn:
        return jsonify({'error': 'Database connection failed'})
    
    try:
        cursor = conn.cursor()
        cursor.execute("""
            SELECT timestamp, mode, final_temp, final_humidity 
            FROM sensor_readings 
            ORDER BY timestamp DESC 
            LIMIT 5
        """)
        
        data = cursor.fetchall()
        
        # Format for HTML table
        formatted_data = []
        for row in data:
            formatted_data.append({
                'timestamp': row[0].strftime('%Y-%m-%d %H:%M:%S'),
                'mode': row[1],
                'temperature': float(row[2]),
                'humidity': float(row[3])
            })
        
        return jsonify({'data': formatted_data})
        
    except Exception as e:
        return jsonify({'error': str(e)})
    finally:
        conn.close()

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080, debug=True)
