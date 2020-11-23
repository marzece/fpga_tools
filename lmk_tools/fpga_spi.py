import socket

def connect_to_fpga():
    HOST = "192.168.1.10"
    PORT = 4001
    fpga_conn = socket.create_connection((HOST, PORT))
    #Do I need to error check? Idk fuckit dude
    return fpga_conn


def decode_data(data):
    data = data.decode("ascii")
    data = data[:data.find("\n")]
    data = int(data, 16) # assumes response is in hex
    return data

def spi_command(server, command):
    command = command.encode('ascii')
    #print(command)
    server.sendall(command)
    # Get response
    resp = server.recv(1024)
    #print(resp)
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

def adc_spi(server, v1, v2, v3):
    return spi_command(server, "write_adc_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))

def lmk_spi(server, v1, v2, v3):
    return spi_command(server, "write_clk_spi 0x%x 0x%x 0x%x\n" % (v1, v2, v3))
