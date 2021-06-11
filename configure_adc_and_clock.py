import socket

import argparse
from time import sleep
from collections import defaultdict
from ceres_fpga_spi import adc_spi, lmk_spi, decode_data, connect_to_local_client, SPI_Device, adc_hard_reset

def parse_config_file(f):
    evm_devices = ["LMK", "ADS"]
    lines = list(f)
    lines = [l[: l.find('//')] for l in lines] # remove comments
    lines = list(filter(lambda x: x, lines)) # remove empty lines
    current_device = None
    instructions = list()
    add_pauses = False
    for line in lines:
        device = [x for x in evm_devices if x in line]
        if(device):
            current_device = device[0]
            continue
        try:
            addr, val = line.split()
        except ValueError:
            addr = line.split()[0]
            # The only command that has no arguement is 'pause'
            # should this be ignored if 'add_pauses" is on?
            if(addr.lower() != "pause"):
                raise ValueError
            instructions.append((current_device, "pause", None))
            continue
        if("sleep" in addr.lower()):
            instructions.append((current_device, "sleep", float(val)))
            continue
        elif("auto-pause" in addr.lower()):
            add_pauses = False
            if(val.lower() == "start" or val.lower == "on"):
                add_pauses = True
            continue
        addr_base = 16 if 'x' in addr else 10
        val_base = 16 if 'x' in val else 10
        instructions.append((current_device, int(addr, addr_base), int(val, val_base)))
        if(add_pauses):
            instructions.append((current_device, "pause", None))
    return instructions

def do_programming(server, adcs, instructions):
    lmks = []
    if(SPI_Device.ADC_A in adcs or SPI_Device.ADC_B in adcs):
        lmks.append(SPI_Device.LMK_A)
    if(SPI_Device.ADC_C in adcs or SPI_Device.ADC_D in adcs):
        lmks.append(SPI_Device.LMK_B)

    for device, addr, value in instructions:
        if(addr == "sleep"):
            print("SLEEPING %f seconds" % value)
            sleep(value)
            continue
        if(addr == "pause"):
            input("Paused, press enter to continue")
            continue
        #print(device, hex(addr), hex(value))
        #input("Paused, press enter to continue")
        if(device.lower()== "lmk"):
            for this_lmk in lmks:
                program_clock(server, this_lmk, addr, value)
        if(device.lower() == "ads"):
            for this_adc in adcs:
                if not this_adc:
                    continue
                program_adc(server, this_adc, addr, value)

def program_clock(server, which_lmk, addr, value):
    # First make sure instructions are in order
    # Remove anything that locks/unlocks stuff, this function handles that

    resp = lmk_spi(server, which_lmk, 0x0, addr, value);
    #resp = spi_command(server, "write_clk_spi 0x%x 0x%x 0x%x\n" % (0x0, addr, value))
    return resp

def program_adc(server, which_adc, addr, value):
    # First, need to identify if this is a analog or digital or neither address
    #if(addr < 0x4FFF):
    if(addr < 0xF00):
        # General registers
        wmpch = 0
        if(addr < 0xFFF):
            # Need to come up with the first 4 bits here
            wmpch = 0x0
        else:
            wmpch = (addr & 0xF000)>>12
            addr = (addr & 0xFFF)
        adc_spi(server, which_adc, wmpch, addr, value);

    elif(addr > 0x0F00 and addr < 0xF000):
        # Analog registers
        page_addr = (addr & 0xFF00)>> 8
        reg_addr = (addr & 0x00FF)
        adc_spi(server, which_adc, 0x0, 0x11, page_addr)
        adc_spi(server, which_adc, 0x0, reg_addr, value)
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
        adc_spi(server, which_adc, 0x4, 0x3, page_addr1)
        adc_spi(server, which_adc, 0x4, 0x4, page_addr2)
        adc_spi(server, which_adc, wmpch, reg_addr, value)
        adc_spi(server, which_adc, wmpch|0x1, reg_addr, value)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", type=str, help="file that contains Eval board addresses and data")
    parser.add_argument("--adc_a", action="store_true", help="send commands to ADC A")
    parser.add_argument("--adc_b", action="store_true", help="send commands to ADC B")
    parser.add_argument("--adc_c", action="store_true", help="send commands to ADC C")
    parser.add_argument("--adc_d", action="store_true", help="send commands to ADC D")
    parser.add_argument("--ti", action="store_true", help="Use commands for TI board")
    parser.add_argument("--do_reset", action="store_true", help="Do hard reset (applies to both ADCs)")

    args = parser.parse_args()

    fn = args.filename

    with open(fn, 'r') as f:
        instructions = parse_config_file(f)

    fpga_conn = connect_to_local_client()

    devices = [SPI_Device.ADC_A if args.adc_a else None,
               SPI_Device.ADC_B if args.adc_b else None,
               SPI_Device.ADC_C if args.adc_c else None,
               SPI_Device.ADC_D if args.adc_d else None]

    print(devices)
    if(args.do_reset):
        print("Doing reset")
        adc_hard_reset(fpga_conn, devices)

    do_programming(fpga_conn, devices, instructions)

    #fpga_conn[0].write("jesd_sys_reset\n".encode("ascii"))
    #_ = fpga_conn[1].readline()
