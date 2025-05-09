import serial
import pymysql
import re
import time
from datetime import datetime

# Configuration
DEVICE = '/dev/ttyACM0'  # Change this to match your Arduino's port
BAUD_RATE = 9600
DB_HOST = 'localhost'
DB_USER = 'pi'          # Change to your MySQL username
DB_PASSWORD = ''        # Change to your MySQL password if any
DB_NAME = 'enviroguardian_db'

# Regular expression to parse the serial output
data_pattern = re.compile(
    r'Mode: (\w+) \| Pot: (\d+)% \| RawTemp: ([\d.]+)°C \| FinalTemp: ([\d.]+)°C \| '
    r'RawHum: ([\d.]+)% \| FinalHum: ([\d.]+)% \| (.+)'
)

def setup_database():
    """Create the database and table if they don't exist."""
    try:
        # Connect to MySQL server without specifying database
        conn = pymysql.connect(host=DB_HOST, user=DB_USER, password=DB_PASSWORD)
        cursor = conn.cursor()
        
        # Create database if it doesn't exist
        cursor.execute(f"CREATE DATABASE IF NOT EXISTS {DB_NAME}")
        cursor.execute(f"USE {DB_NAME}")
        
        # Create table for sensor readings
        cursor.execute('''
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id INT AUTO_INCREMENT PRIMARY KEY,
            timestamp DATETIME NOT NULL,
            mode VARCHAR(10) NOT NULL,
            pot_value INT NOT NULL,
            raw_temp FLOAT NOT NULL,
            final_temp FLOAT NOT NULL,
            raw_humidity FLOAT NOT NULL,
            final_humidity FLOAT NOT NULL,
            message VARCHAR(50) NOT NULL
        )
        ''')
        
        cursor.close()
        conn.close()
        print(f"Database '{DB_NAME}' and table 'sensor_readings' setup complete.")
    except pymysql.MySQLError as e:
        print(f"Error setting up database: {e}")
        exit(1)

def main():
    # Set up the database first
    setup_database()
    
    # Connect to the Arduino
    try:
        arduino = serial.Serial(DEVICE, BAUD_RATE)
        print(f"Connected to Arduino on {DEVICE}")
    except serial.SerialException as e:
        print(f"Error connecting to Arduino: {e}")
        return
    
    # Connect to the database
    try:
        dbconn = pymysql.connect(
            host=DB_HOST,
            user=DB_USER,
            password=DB_PASSWORD,
            database=DB_NAME
        )
        print(f"Connected to database '{DB_NAME}'")
        
        while True:
            try:
                # Read data from Arduino
                data = arduino.readline().decode('utf-8').strip()
                print(f"Raw data: {data}")
                
                # Parse the data using regex
                match = data_pattern.match(data)
                if match:
                    mode, pot, raw_temp, final_temp, raw_hum, final_hum, message = match.groups()
                    
                    # Insert into database
                    cursor = dbconn.cursor()
                    cursor.execute(
                        """INSERT INTO sensor_readings 
                           (timestamp, mode, pot_value, raw_temp, final_temp, raw_humidity, final_humidity, message)
                           VALUES (%s, %s, %s, %s, %s, %s, %s, %s)""",
                        (
                            datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                            mode,
                            int(pot),
                            float(raw_temp),
                            float(final_temp),
                            float(raw_hum),
                            float(final_hum),
                            message
                        )
                    )
                    dbconn.commit()
                    cursor.close()
                    print(f"Data stored: Mode={mode}, Temp={final_temp}°C, Humidity={final_hum}%")
                else:
                    print("Data format not recognized.")
                    
            except KeyboardInterrupt:
                print("\nProgram terminated by user")
                break
            except Exception as e:
                print(f"Error: {e}")
                time.sleep(1)  # Pause before retrying
                
    except pymysql.MySQLError as e:
        print(f"Database error: {e}")
    finally:
        if 'dbconn' in locals() and dbconn.open:
            dbconn.close()
            print("Database connection closed")
        if 'arduino' in locals():
            arduino.close()
            print("Arduino connection closed")

if __name__ == "__main__":
    main()
