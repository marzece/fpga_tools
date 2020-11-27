import socket

def connect_to_local_client():
    resp_pipe = open("kintex_response_pipe", "r")
    command_pipe = open("kintex_command_pipe", "wb", 0)
    return command_pipe, resp_pipe

def connect_to_fpga():
    HOST = "192.168.1.10"
    PORT = 4001
    if DEBUG_OUTP:
        return open("fpga_spi_debug.txt", "w");

    fpga_conn = socket.create_connection((HOST, PORT))
    #Do I need to error check? Idk fuckit dude
    return fpga_conn


def decode_data(data):
    try:
        data = data.decode("ascii")
    except AttributeError:
        pass
    data = data[:data.find("\n")]
    data = int(data, 16) # assumes response is in hex
    return data

def spi_command(server, command):
    pop_command = "lmk_spi_data_pop" if "lmk" in command else "ads_a_spi_pop"
    command = command.encode("ascii")
    pop_command = pop_command.encode("ascii")
    server[0].write(command)
    server[1].readline()
    
    resp = [0, 0, 0]
    server[0].sendall(pop_command)
    resp[0] = decode_data(server[1].readline())
    server[0].sendall(pop_command)
    resp[1] = decode_data(server[1].readline())
    server[0].sendall(pop_command)
    resp[2] = decode_data(server[1].readline())

    return resp


def adc_spi(server, v1, v2, v3):
    return spi_command(server, "ads_a_spi_data_available 0x%x 0x%x 0x%x\n" % (v1, v2, v3))
    #return spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))

def lmk_spi(server, v1, v2, v3):
    return spi_command(server, "write_lmk_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))
    #return spi_command(server, "write_clk_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))
