import socket
import hiredis
from time import sleep
from enum import Enum

class SPI_Device(Enum):
    ADC_A = 1
    ADC_B = 2
    ADC_C = 3
    ADC_D = 4
    LMK_A = 5
    LMK_B = 6
    CERES_LMK = 7
    TI_ADC = 8

# Map from enum to server id
server_device_ids = { SPI_Device.ADC_A: 0, SPI_Device.ADC_B:1,
                      SPI_Device.ADC_C:2, SPI_Device.ADC_D:3,
                      SPI_Device.LMK_A:0, SPI_Device.LMK_B:1,
                      SPI_Device.TI_ADC:0,
                      SPI_Device.CERES_LMK: 2}

def connect_to_fpga(ip="localhost", port=4002):
    fpga_conn = socket.create_connection((ip, port))
    #Do I need to error check? Idk fuckit dude
    return fpga_conn

def grab_response(server):
    t=False
    resp_reader = hiredis.Reader() # TODO (just make this global?)
    while t is False:
        resp_reader.feed(server.recv(1024))
        t = resp_reader.gets()
    return t

def decode_data(data):
    try:
        data = data.decode("ascii")
    except AttributeError:
        pass
    data = data[:data.find("\n")]
    data = int(data, 16) # assumes response is in hex
    return data

def is_lmk_device(device):
    return device in [ SPI_Device.LMK_A, SPI_Device.LMK_B, SPI_Device.CERES_LMK]

def spi_command(server, device, *args):
    adc_write_command = "write_adc_spi %i" % server_device_ids[device]
    adc_pop_cmd = "ads_spi_pop %i\r\n" % server_device_ids[device]
    lmk_write_command = "write_lmk_spi %i" % server_device_ids[device]
    lmk_pop_cmd = "lmk_spi_data_pop %i\r\n" % server_device_ids[device]

    arg_string = " ".join([hex(x) for x in args])
    write_command = lmk_write_command if is_lmk_device(device) else adc_write_command
    write_command = "%s %s\r\n" % (write_command, arg_string)
    pop_command = lmk_pop_cmd if is_lmk_device(device) else adc_pop_cmd

    write_command = write_command.encode("ascii")
    pop_command = pop_command.encode("ascii")

    server.sendall(write_command)
    _ = grab_response(server)
    
    resp = [0, 0, 0]

    server.sendall(pop_command)
    resp[0] = grab_response(server)

    server.sendall(pop_command)
    resp[1] = grab_response(server)

    server.sendall(pop_command)
    resp[2] = grab_response(server)
    return resp

def adc_hard_reset(server, devices):
    bit_mask = 0x0
    if SPI_Device.ADC_A in devices:
        bit_mask |= 0x1
    if SPI_Device.ADC_B in devices:
        bit_mask |= 0x2
    if SPI_Device.ADC_C in devices:
        bit_mask |= 0x4
    if SPI_Device.ADC_D in devices:
        bit_mask |= 0x8

    reset_command = ("adc_reset 0x%x\r\n" % bit_mask).encode("ascii")
    unreset_command = "adc_reset 0x0\r\n".encode("ascii")
    server.sendall(reset_command)
    grab_response(server)
    sleep(0.2)
    server.sendall(unreset_command)
    grab_response(server)
    sleep(1.0)

def adc_spi(server, device, v1, v2, v3):
    if(device not in [SPI_Device.ADC_A, SPI_Device.ADC_B, SPI_Device.ADC_C, SPI_Device.ADC_D]):
        raise TypeError("ADC device not correctly specified for adc spi function")
    return spi_command(server, device, v1, v2, v3)

def lmk_spi(server, device, v1, v2, v3):
    return spi_command(server, device, v1, v2, v3)
