import socket

import argparse
from time import sleep
from collections import defaultdict
import matplotlib.pyplot as plt

def decode_data(data):
    data = data.decode("ascii")
    data = data[:data.find("\n")]
    data = int(data, 16) # assumes response is in hex
    return data

def parse_config_file(f):
    evm_devices = ["LMK", "ADS"]
    lines = list(f)
    lines = [l[: l.find('//')] for l in lines] # remove comments
    lines = list(filter(lambda x: x, lines)) # remove empty lines
    current_device = None
    instructions = list()
    for line in lines:
        device = [x for x in evm_devices if x in line]
        if(device):
            current_device = device[0]
            continue
        addr, val = line.split()
        if("sleep" in addr.lower()):
            instructions.append((current_device, "sleep", float(val)))
            continue
        addr_base = 16 if 'x' in addr else 10
        val_base = 16 if 'x' in val else 10
        instructions.append((current_device, int(addr, addr_base), int(val, val_base)))
    return instructions

def spi_command(server, command):
    command = command.encode('ascii')
    print(command)
    server.sendall(command)
    # Get response
    resp = server.recv(1024)
    print(resp)
    # Now read whatever was on the SPI in line
    server.sendall(b"spi_pop\n")
    data = server.recv(1024)
    d1 = decode_data(data)
    server.sendall(b"spi_pop\n")
    data = server.recv(1024)
    d2 = decode_data(data)
    server.sendall(b"spi_pop\n")
    data = server.recv(1024)
    d3 = decode_data(data)
    return d1, d2, d3

def do_programming(server, instructions):
    for device, addr, value in instructions:
        if(addr == "sleep"):
            print("SLEEPING %f seconds" % value)
            sleep(value)
            continue
        if(device == "LMK"):
            program_clock(server, addr, value)
        if(device == "ADS"):
            program_adc(server, addr, value)

def program_clock(server, addr, value):
    # First make sure instructions are in order
    # Remove anything that locks/unlocks stuff, this function handles that

    print(addr, value)
    resp = spi_command(server, "write_clk_spi 0x%x 0x%x 0x%x\n" % (0x0, addr, value))
    print(resp)

def program_adc(server, addr, value):
    # First, need to identify if this is a analog or digital or neither address
    if(addr < 0x4FFF):
        # General registers
        wmpch = 0
        if(addr < 4095):
            # Need to come up with the first 4 bits here
            wmpch = 0x0
        else:
            wmpch = (addr & 0xF000)>>12
            addr = (addr & 0xFFF)
        spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (wmpch, addr, value))

    elif(addr > 0x8000 and addr < 0xF000):
        # Analog registers
        page_addr = (addr & 0xFF00)>> 8
        reg_addr = (addr & 0x00FF)
        spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x0, 0x11, page_addr)),
        spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x0, reg_addr, value)),
    else:
        # Must be digital (TODO add more validation)
        page_addr = (addr & 0xFFFF00) >> 8
        page_addr1 = (page_addr & 0xFF)
        page_addr2 = (page_addr & 0xFF00) >> 8
        reg_addr = (addr & 0xFF)
        wmpch = page_addr2 >> 4

        # if setting CH bit is set, remove it for writting the address
        page_addr2 = (page_addr2 & 0xEF)

        # Not sure about the wmpch values here
        spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x4, 0x3, page_addr1))
        spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x4, 0x4, page_addr2))
        spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (wmpch, reg_addr, value))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", type=str, help="file that contains Eval board addresses and data")

    args = parser.parse_args()

    fn = args.filename

    with open(fn, 'r') as f:
        instructions = parse_config_file(f)

    host = "192.168.1.10"
    port = 4001
    fpga_conn = socket.create_connection((host, port))

    do_programming(fpga_conn, instructions)
