import socket
import hiredis
from enum import Enum

class SPI_Device(Enum):
    LMK=1
    ADC_A=2
    ADC_B=3
    PREAMP_DAC=4
    TI_ADC=5

def connect_to_local_client():
    command_pipe = open("/Users/ericmarzec/fpga_tools/fakernet_code/kintex_command_pipe", "wb", 0)
    resp_pipe = open("/Users/ericmarzec/fpga_tools/fakernet_code/kintex_response_pipe", "rb")
    return command_pipe, resp_pipe

def connect_to_fpga():
    HOST = "192.168.1.10"
    PORT = 4001
    if DEBUG_OUTP:
        return open("fpga_spi_debug.txt", "w");

    fpga_conn = socket.create_connection((HOST, PORT))
    #Do I need to error check? Idk fuckit dude
    return fpga_conn

def grab_response(server):
    t=False
    resp_reader = hiredis.Reader() # TODO (just make this global?)
    while t is False:
        resp_reader.feed(server[1].readline())
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

def spi_command(server, device, *args):
    adc_a_write_command = "write_a_adc_spi"
    adc_b_write_command = "write_b_adc_spi"
    ti_write_command = "write_adc_spi"
    lmk_write_command = "write_lmk_spi"
    adc_a_pop_cmd = "ads_a_spi_pop\n"
    adc_b_pop_cmd = "ads_b_spi_pop\n"
    lmk_pop_cmd = "lmk_spi_data_pop\n"
    ti_pop_cmd = "ads_spi_pop\n"

    write_command_dict = {SPI_Device.LMK : lmk_write_command,
                          SPI_Device.ADC_A : adc_a_write_command,
                          SPI_Device.ADC_B : adc_b_write_command,
                          SPI_Device.PREAMP_DAC : None,
                          SPI_Device.TI_ADC: ti_write_command}
    spi_pop_command_dict = {SPI_Device.LMK : lmk_pop_cmd,
                            SPI_Device.ADC_A : adc_a_pop_cmd,
                            SPI_Device.ADC_B : adc_b_pop_cmd,
                            SPI_Device.PREAMP_DAC : None,
                            SPI_Device.TI_ADC: ti_pop_cmd}

    arg_string = " ".join([hex(x) for x in args])
    write_command = write_command_dict[device]
    write_command = "%s %s\n" % (write_command, arg_string)
    pop_command = spi_pop_command_dict[device]

    write_command = write_command.encode("ascii")
    pop_command = pop_command.encode("ascii")

    server[0].write(write_command)
    _ = grab_response(server)

    resp = [0, 0, 0]
    server[0].write(pop_command)
    resp[0] = grab_response(server)
    #resp[0] = decode_data(server[1].readline())
    server[0].write(pop_command)
    resp[1] = grab_response(server)
    #resp[1] = decode_data(server[1].readline())
    server[0].write(pop_command)
    resp[2] = grab_response(server)
    #resp[2] = decode_data(server[1].readline())
    return resp

def adc_spi(server, device, v1, v2, v3):
    if(device not in [SPI_Device.ADC_A, SPI_Device.ADC_B, SPI_Device.TI_ADC]):
        raise TypeError("ADC device not correctly specified for adc spi function")
    return spi_command(server, device, v1, v2, v3)

def lmk_spi(server, v1, v2, v3):
    return spi_command(server, SPI_Device.LMK, v1, v2, v3)
    #return spi_command(server, "write_lmk_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))
    #return spi_command(server, "write_clk_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))
