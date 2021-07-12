from ceres_fpga_spi import decode_data, connect_to_local_client, grab_response
from time import sleep

REGMAP_FILENAME = "/Users/ericmarzec/fpga_tools/regmap.txt"

def read_reg_file(fn):
    lines  = [x.split() for x in list(open(fn))]
    return {int(x[0].replace("R","")): int(x[1], 16) & 0xFFFF for x in lines}

def read_regmap_file(fn=REGMAP_FILENAME):
    lines  = [x.split() for x in list(open(fn))]
    lines = {int(x[0]): [int(v) if v in '01' else v for v  in x[1:]] for x in lines}
    return lines

# From a register config file create an EEPROM image
def produce_eeprom_layout(regs, regmap):
    result = {}
    for key, values in regmap.items():
        #new_values = []
        new_word = 0x0
        for i, v in enumerate(values):
            new_word = new_word << 1
            if v == 0 or v == 1:
                #new_values.append(v)
                new_word |= v
            elif type(v)==str and 'R' in v and "CRC" not in v:
                reg_number = int(v[1:v.find('[')])
                bit_index = int(v[v.find('[')+1 : v.find(']')])
                bit = (regs[reg_number] >> bit_index) & 0x1
                #new_values.append(bit)
                new_word |= bit

        result[key] = new_word
    return result

# From an EEPROM image produce list of registers
def eeprom_to_registers(image, regmap):
    result = {i:0 for i in range(86)}
    for addr, (readback, rmap) in enumerate(zip(image, regmap.values())):
        for i, v in enumerate(rmap):
            if v == 0 or v == 1:
                bit = ((readback >> i) & 0x1)
                if (bit != v):
                    print("Fixed bit mismatch. Addr=%i Bit=%i" % (addr, i))
            elif type(v)==str and 'R' in v and "CRC" not in v:
                reg_number = int(v[1:v.find('[')])
                bit_index = int(v[v.find('[')+1 : v.find(']')])
                bit = ((readback >> i) & 0x1)
                if(bit ==1):
                    result[reg_number] ^= (1<<bit_index)
    return result

def write_iic_register(fpga, addr, value):
    write_clkgen_command = "write_clkgen_register {addr} {value}"
    this_command = write_clkgen_command.format(addr=addr, value=value)
    fpga[0].write(this_command.encode("ascii"))
    return grab_response(fpga)

def read_iic_register(fpga, addr, value):
    read_clkgen_command = "read_clkgen_register {addr}"
    this_command = read_clkgen_command.format(addr=addr)
    fpga[0].write(this_command.encode("ascii"))
    return grab_response(fpga)

def set_clkgen_gpio(fpga, val):
    clkgen_gpio_command = "write_clkgen_gpio {value}"
    fpga[0].write(clkgen_gpio_command.encode('ascii'))
    return grab_response(fpga)

def do_power_cycle(fpga):
    # Finally power-down then power-up the CDCE chip
    set_clkgen_gpio(fpga, 0x0);
    sleep(1.0)
    set_clkgen_gpio(fgpa, 0x7)

def write_eeprom(fpga, image):
    NVM_WRITE_ADDR_LOC = 0xD
    NVM_WRITE_DATA_LOC = 0xE
    # Unlock the EEPROM by setting EE_LOCK = 0x5 
    # EE_LOCK is bits 12-15 (inclusive) of Reg15 (addr=0xF)
    _ = write_iic_register(fpga, 0xf, 0x5000)

    sleep(0.1)

    _ = write_iic_register(fpga, NVM_WRITE_ADDR_LOC, 0x0)

    sleep(0.1)

    for addr, value in sorted(eeprom_image.items(), key=lambda x: x[0]):
        _ = write_iic_register(fpga, NVM_WRITE_DATA_LOC, value)
        sleep(0.1)

    do_power_cycle(fpga)

def readback_eeprom(fpga):
    NVM_READ_ADDR_LOC = 0xB
    NVM_READ_DATA_LOC = 0xC
    NUM_REGISTERS = 64

    image = {}
    _ = write_iic_register(NVM_READ_ADDR_LOC, 0x0);
    sleep(0.1)
    for i in range(NUM_REGISTERS):
        image[i] = read_iic_register(NVM_READ_DATA_LOC)
        sleep(0.1)

    return image

def program_registers(fpga, regs):
    for addr, value in regs.items():
        _ = write_iic_register(fpga, addr, value)
        sleep(0.1)

def readback_registers(fpga):
    NUM_REGS = 86
    regs = {}
    for addr in range(86):
        regs[addr] = read_iic_register(fpga, addr)
        sleep(0.1)
    return regs

def perform_reg_commit_operation(fpga):
    # Once re-configured write to the RECAL VCO register (Bit 4 of R0)
    reg0 = read_iic_register(fpga, 0x0)
    reg0 |= 1<<4
    sleep(0.1)
    _ = write_iic_register(fpga, 0x0, reg0)

    # Make sure the REGCOMMIT_PAGE is set to 0b1 (the page selected by the resistor
    # pull-up(down) on the HWSW_SELECT pin)
    # REGCOMMIT_PAGE is bit-9 of R3  0b0 selects page 0, 0b1 selects page 1
    REG_R3_VALUE = 1<<9
    _ = write_iic_register(fpga, 0x3, REG_R3_VALUE)
    sleep(0.1)

    # Unlock the EEPROM by setting EE_LOCK = 0x5 
    # EE_LOCK is bits 12-15 (inclusive) of Reg15 (addr=0xF)
    _ = write_iic_register(fpga, 0xf, 0x5000)
    sleep(0.1)
    # Write 1 to the REGCOMMIT bit. REGCOMMIT is bit 10 of R3
    # Need to be sure not to stomp on the REGCOMMIT_PAGE selection above
    REG_R3_VALUE |= (1<<10)
    write_iic_register(fpga, 0x3, REG_R3_VALUE)
    sleep(0.1)

    # Last programming step need to do nonsense with the CRC (idk if this is actually needed)


    # Finally power-down then power-up the CDCE chip
    do_power_cycle(fpga)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", type=str, help="Config file for all the CDCE6214 registers")
    parser.add_argument("--direct_commit", action="store_true", help="")
    parser.add_argument("--reg_commit", action="store_true", help="")
    parser.add_argument("--program_regs", action="store_true", help="")

    args = parser.parse_args()

    if((args.direct_commit  and args.reg_commit) or\
       (args.direct_commit and args.program_regs) or\
       (args.reg_commit and args.program_regs)):
        print("Can only have a single programming method specified!")
        exit(0)

    fpga = connect_to_local_client()
    regs = read_reg_file(args.filename)
    reg_map = read_regmap_file()

    if(args.direct_commit):
        print("Doing Direct Commit")
        eeprom_image = produce_eeprom_layout(regs, reg_map)
        write_eeprom(fpga, eeprom_image)
        sleep(1.0)
        readback = readback_eeprom(fpga)
        for reg, rb in enumerate(read_back):
            if(rb != eeprom_image[reg]):
                print("0x%x\tSet: 0x%x\tReadback: 0x%x" % (reg, eeprom_image[reg], rb))

    if(args.program_regs):
        print("Programming Registers")
        program_registers(fpga, regs)

    if(args.reg_commit):
        print("Doing Reg Commit")
        perform_reg_commit_operation(fgpa)



