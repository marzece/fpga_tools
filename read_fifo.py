import socket
from time import sleep, time
import matplotlib.pyplot as plt
from collections import defaultdict
import struct
import numpy as np

def decode_data(data):
    data = data.decode("ascii")
    data = data[:data.find("\n")]
    data = int(data, 16) # assumes response is in hex

    return data

def pprint_time(ns):
    # Assumes time is in nanoseconds
    seconds, ns = divmod(ns, 1e9)
    ms, ns = divmod(ns, 1e6)
    us, ns = divmod(ns, 1e3)
    ret = "(%i seconds %i ms %i us %i ns)" % (seconds, ms, us, ns)
    return ret

def read_data(conn):
    header = struct.Struct("IIQ") # TODO find fixed widith type equivalents
    start_time = time()
    conn.sendall(b"readout_event\n")
    data = conn.recv(header.size) # get the trigger header info
    word, length, clock = header.unpack(data)
    num_channels = 16
    data_remaining = 4*length*num_channels
    print(word, length, clock)
    buffer = b'';
    i =0
    while(data_remaining > 0):
        new_data = conn.recv(data_remaining)
        data_remaining -= len(new_data)
        buffer += new_data
        i+=1
    print(i)
    end_time = time()
    print("readout time = %g" % (end_time - start_time,))

    return word, length, clock, buffer

def event_data_to_samples(buf):
    num_samples = len(buf)/4
    samples = []
    vals = struct.unpack("%iI" % num_samples, buf);
    for v in vals:
        v1 = v & 0xFFFF
        v2 = v & 0xFFFF0000 >> 16
        samples.append(v1)
        samples.append(v2)

    return np.reshape(samples, (16,-1))

clock_store = 0;
time_per_clock = 2000/(122.88*4) # ns per 2 samples
host = "192.168.1.10"
port = 4001
conn = socket.create_connection((host, port))

#conn.sendall(b"write_threshold 4250000")
#conn.recv(1024)

#conn.sendall(b"set_timer_freq 10000\n")
#conn.recv(1024)

if __name__ == "__main__":
    CHANNEL_UT = 0
    while(True):
        #conn.sendall(b"valve_write 0")
        #print(conn.recv(1024))
        #conn.sendall(b"write_threshold 1 0xFFFFFFFF\n")
        conn.sendall(b"write_threshold 0 0x42000\n")
        print(conn.recv(1024))
        #conn.sendall(b"valve_write 65535")
        #print(conn.recv(1024))
        while(1):
            conn.sendall(b"trig_fifo_data_avail\n")
            data = conn.recv(1024)
            data = decode_data(data)
            if(data != 0):
                break
            print("No data available")
            sleep(0.1)

        conn.sendall(b"write_threshold 0 0xffffffff\n")
        conn.recv(1024)
        trig_word, trig_length, clock_val, raw_data =  read_data(conn)
        data = event_data_to_samples(raw_data)
        plt.plot(data.swapaxes(0,1))


        plt.show()
        input("Continue?")
        plt.close()
        sleep(0.5)
