import fpga_spi
import hiredis
import redis
from time import sleep, time
import socket

resp_reader = hiredis.Reader()

#TODO MOVE THESE TO A SEPERATE MODULE!!!
def grab_response(server):
    t=False
    resp_reader.feed(server.recv(1024))
    t = resp_reader.gets()
    return t

def connect_to_local_client(host='localhost', port=4003):
    fpga_conn = socket.create_connection((host, port))
    return fpga_conn

def send_command(server, command):
    server.sendall(command.encode('ascii'))
    return grab_response(server)

def publish_data(db, data):
    db.publish("slow_monitor", str(data))

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--ceres", action="store_true", help="Uses CERES addresses & config")
    parser.add_argument("--kcu", action="store_true", help="Uses KCU/HE2TER adresses & config")
    parser.add_argument("-addr", type=str, help="IIC address for ADC")
    parser.add_argument("--port", type=int, help="Server Port", default=4003)
    args = parser.parse_args()

    server = connect_to_local_client(port=args.port)

    redis_db = redis.Redis()


    #current_measurement_resistor = 0.0005 # units ohms, from CERES schematics
    current_measurement_resistor = 0.006 # units ohms, from CERES schematics
    if args.kcu:
        current_measurement_resistor = 0.006 # units ohms, from HE2TER schematics

    current_measurement_LSB = 20e-6 # 20uV per LSB, from LTC4151 datasheet
    voltage_measurement_LSB = 25e-3 # 25mV per LSB, from LTC4151 datasheet
    current_adc_conv = current_measurement_LSB/current_measurement_resistor

    #monitor_iic_addr = 0xD6 # CERES, HERMES slot 1
    monitor_iic_addr = 0xD4 # CERES, HERMES slot 0
    if(args.kcu):
        monitor_iic_addr = 0xD4

    if(args.addr is not None):
        if("0x" == args.addr[0:2]):
            monitor_iic_addr = int(args.addr, 16)
        else:
            monitor_iic_addr = int(args.addr)

    read_vm_command = "read_iic_bus_with_reg "+str(monitor_iic_addr) +"  {iic_reg}\r\n"

    # First set the IIC MUX
    # I'm going to assume the IIC mux will never be screwed with
    # by some other process. So I'll just write it once and never again
    # IIC MUX IIC ADDR = 0xEA
    # 0x5 is the correct data value for talking to the FMC IIC
    if args.kcu:
        _ = send_command(server, "write_iic_bus 0xEA 0x5\r\n")
        value = send_command(server, "read_iic_bus 0xEA\r\n")
        if(value != 0x5):
            print("Failed to write IIC Mux!!!!")
            print(value)
            import ipdb;ipdb.set_trace()
            exit()

    # TODO come up with some way book keeping this better, i.e namedtuple
    # or something, except tuples suck so idk. Maybe just create a Class
    vm_info = {'current': {"addrs": [0x0, 0x1], "values":[0,0]},
               'voltage': {"addrs": [0x2, 0x3], "values":[0,0]},
               'temp': {"addrs": [0x4, 0x5], "values":[0,0]}}

    current = None
    voltage = None
    #import ipdb;ipdb.set_trace()
    while(True):
        for _, regs in vm_info.items():
            for i, reg in enumerate(regs['addrs']):
                regs['values'][i] = send_command(server, read_vm_command.format(iic_reg=reg))
                sleep(0.25)

        voltage_values = vm_info["voltage"]["values"]
        voltage_valid = (voltage_values[1] & (1<<3)) == 0
        if(voltage_valid):
            voltage = (voltage_values[0] << 4) | (voltage_values[1]>>4)
            voltage *=  voltage_measurement_LSB
        else:
            print("Invalid voltage")
            voltage = None

        current_values = vm_info["current"]["values"]
        current_valid = (current_values[1] & (1<<3)) == 0
        if(current_valid):
            current = (current_values[0] << 4) | (current_values[1] >> 4)
            current *= current_adc_conv
        else:
            print("Invalid current")
            current = None
            # Not valid TODO
        temp_values = vm_info["temp"]["values"]
        temp_valid = (temp_values[1] & (1<<3)) == 0
        if(temp_valid):
            temp = (temp_values[0] << 4) | (temp_values[1] >> 4)

        if(current is not None):
            print("Current = %0.3f A\t" % current, end='')
        if(voltage is not None):
            print("Voltage = %0.3f V\t" % voltage, end = '')
        if(temp_valid):
            print("Temp = 0x%x " % temp, end='')

        print('')

        if(voltage and current):
            #redis_db.xadd("
            publish_data(redis_db, "%f, %f, %f" % (time(), voltage, current))
        
        sleep(2)

