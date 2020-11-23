# This file is meant to hold functions for connecting to the FPGA
# then engaging the special 'ping' modes and then taking a number of
# pings and reading out the data and finally presenting the results to
# the command line
import socket
from time import sleep
import numpy as np

def connect_to_fpga():
    HOST = "192.168.1.10"
    PORT = 4001
    fpga_conn = socket.create_connection((HOST, PORT))
    #Do I need to error check? Or does python just handle that for me. Idk fuckit dude
    return fpga_conn

def decode_data(data):
    data = data.decode("ascii")
    data = data[:data.find("\n")]
    if(data.isalpha()):
        return data
    data = int(data, 16) # assumes response is in hex. TODO should generalize
    return data

def set_ping_mode(conn, mode):
    error = 0
    produce_trigger_string = ("write_produce_triggers 0x10 %i\n" % mode).encode("ascii")
    node_trigger_string = ("write_node_triggers 0x18 %i\n" % mode).encode("ascii")
    conn.sendall(produce_trigger_string)
    data = conn.recv(1024)
    data = decode_data(data)
    if(data != "OK"):
        print("Error setting 'produce_triggers' block into ping mode")
        error += 1
    conn.sendall(b"write_produce_triggers 0x10 0x1\n")
    data = conn.recv(1024)
    data = decode_data(data)
    if(data != "OK"):
        print("Error setting 'node_triggers' block into ping mode")
        error += 1
    return error

def set_punchit(conn, value):
    punchit_string = ("write_produce_triggers 0x18 %i\n" % value).encode('ascii')
    conn.sendall(punchit_string)
    data = decode_data(conn.recv(1024))
    if(data != "OK"):
        print("Error setting 'punchit' in produce_trigger")
        return 1
    return 0

def read_pings_available(conn, target):
    if(target == "produce"):
        command = ("read_produce_trigger_fifo 0x1C\n").encode("ascii")
    elif(target == "node"):
        command = ("read_node_trigger_fifo 0x1C\n").encode("ascii")
    else:
        print("Unknown target %s" % str(target))
        return 0
    conn.sendall(command)
    data = decode_data(conn.recv(1024))
    return data

def read_ping_data(conn, target):
    if(target == "produce"):
        command = ("read_produce_trigger_fifo 0x20\n").encode("ascii")
    elif(target == "node"):
        command = ("read_node_trigger_fifo 0x20\n").encode("ascii")
    else:
        print("Unknown target %s" % str(target))
        return 0

    conn.sendall(command)
    data = decode_data(conn.recv(1024))
    return data

def empty_ping_fifo(conn, target):
    if(target == "produce"):
        command = ("write_produce_trigger_fifo 0x18\n").encode("ascii")
    elif(target == "node"):
        command = ("write_node_trigger_fifo 0x18\n").encode("ascii")
    else:
        print("Unknown target %s" % str(target))
        return 1
    conn.sendall(command)
    return decode_data(conn.recv(1024))
    return 0

if __name__ == "__main__":
    fpga = connect_to_fpga()

    producer_clock_speed = 245.76e6 # 200 MHz
    node_clock_speed = 312.5e6  # 156.25 MHz
    set_ping_mode(fpga, 1)

    N_PINGS = 450
    set_punchit(fpga, N_PINGS)
    sleep(1.0)
    set_punchit(fpga, 0)
    npings_producer = read_pings_available(fpga, "produce")
    npings_node = read_pings_available(fpga, "node")

    print("Producer NPINGS = %i" % npings_producer)
    print("Node NPINGS = %i" % npings_node)

    producer_counter_values = np.zeros(npings_producer)
    node_counter_values = np.zeros(npings_node)

    for i in range(npings_producer):
        producer_counter_values[i] = read_ping_data(fpga, "produce")

    for i in range(npings_node):
        node_counter_values[i] = read_ping_data(fpga, "node")


    set_ping_mode(fpga, 0)
    fpga.close()

