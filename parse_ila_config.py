import json
import socket
import copy
from ceres_fpga_spi import connect_to_fpga, grab_response, SPI_Device

def grab_bits(word, first_bit, last_bit):
    mask = 0
    # I'm sure there's a way to do this without looping
    for _ in range(last_bit - first_bit + 1):
        mask = mask << 1
        mask |= 1
    return (word >> first_bit) & mask

def read_reg_info_file(fn):
    f = open(fn)
    json_obj = json.load(f)
    reg_info = json_obj["registers"]
    registers = []
    for reg in reg_info:
        registers.append(JESDReg(**reg))

    return registers

class Field:
    def __init__(self, **kwargs):
        self.name = kwargs['name']
        self.bit_low = kwargs['bit_low']
        self.bit_high = kwargs['bit_high']
        self.description = kwargs.get('description')
        self.translation = kwargs.get('translation')
        if not self.translation:
            self.translation = lambda x: x
        else:
            self.translation = eval(self.translation)
        self.value = None
    def __repr__(self):
        return self.name + " : "+ str(self.value)
    def get_value(self, word):
        self.value = self.translation(grab_bits(word, self.bit_low, self.bit_high))

class JESDReg:
    def __init__(self, **kwargs):
        self.name = kwargs['name']
        addr = int(kwargs['addr'], 16)
        self.raw_value = None
        if(kwargs.get('addr_increment')):
            self.per_lane_reg = True
            incr = int(kwargs["addr_increment"], 16)
            self.addr = [addr + i*incr for i in range(4)]
        else:
            self.per_lane_reg = False
            self.addr = addr


        if(self.per_lane_reg):
            self.fields = [[Field(**x) for x in kwargs['fields']] for _ in self.addr]
        else:
            self.fields = [Field(**x) for x in kwargs['fields']]
        

    def __repr__(self):
        field_strs = [repr(x) for x in self.fields]
        addr_str = "addr = " 
        if(self.per_lane_reg):
            addr_str = addr_str + ', '.join([hex(x) for x in self.addr])
        else:
            addr_str = addr_str + hex(self.addr)
        return self.name + "\n" + str(addr_str) + "\n\t" + '\n\t'.join(field_strs) + "\n"

def write_to_fpga(conn, device, addr, value):
    if(device == SPI_Device.ADC_A):
        command = "jesd_write 0x0 0x%x 0x%x\r\n" % (addr, value)
    elif(device == SPI_Device.ADC_B):
        command = "jesd_b_write 0x1 0x%x 0x%x\r\n" % (addr, value)
    elif(device == SPI_Device.ADC_C):
        command = "jesd_b_write 0x2 0x%x 0x%x\r\n" % (addr, value)
    elif(device == SPI_Device.ADC_D):
        command = "jesd_b_write 0x3 0x%x 0x%x\r\n" % (addr, value)
    elif(device ==SPI_Device.TI_ADC):
        command = "jesd_write 0x%x 0x%x\r\n" % (addr, value)
    else:
        raise Exception("Invalid device specified")
    conn.sendall(command.encode("ascii"))
    data = grab_response(conn)
    return data

def read_from_fpga(conn, device, addr):
    if(device == SPI_Device.ADC_A):
        command = "jesd_read 0x0 0x%x\r\n" % addr
    elif(device == SPI_Device.ADC_B):
        command = "jesd_read 0x1 0x%x\r\n" % addr
    elif(device == SPI_Device.ADC_C):
        command = "jesd_read 0x2 0x%x\r\n" % addr
    elif(device == SPI_Device.ADC_D):
        command = "jesd_read 0x3 0x%x\r\n" % addr
    elif(device == SPI_Device.TI_ADC):
        command = "jesd_read 0x%x\r\n" % addr
    else:
        raise Exception("Invalid device specified")

    conn.sendall(command.encode("ascii"))
    data = grab_response(conn)
    if(type(data) != int):
        import ipdb;ipdb.set_trace()
    return data

def read_reg(conn, device, reg):
    if(reg.per_lane_reg):
        reg.raw_value = []
        for fields, addr in zip(reg.fields, reg.addr):
            word = read_from_fpga(conn, device, addr)
            reg.raw_value.append(word)
            for f in fields:
                f.get_value(word)
    else:
        word = read_from_fpga(conn, device, reg.addr)
        reg.raw_value = word
        for f in reg.fields:
            f.get_value(word)

def translate_debug_word(word):
    ret_strs = ["Start of data was {} detected",
                "Start of ILA was {} detected",
                "Lane {} Code Group Sync",
                "Lane is {} currently recieving K chars"]
    if(word & 0x8):
        ret_strs[0] = ret_strs[0].format("")
    else:
        ret_strs[0] = ret_strs[0].format("NOT")
    if(word & 0x4):
        ret_strs[1] = ret_strs[1].format("")
    else:
        ret_strs[1] = ret_strs[1].format("NOT")
    if(word & 0x2):
        ret_strs[2] = ret_strs[2].format("has")
    else:
        ret_strs[2] = ret_strs[2].format("does NOT have")
    if(word & 0x1):
        ret_strs[3] = ret_strs[3].format("")
    else:
        ret_strs[3] = ret_strs[3].format("NOT")

    return "\n".join(ret_strs)

def translate_link_error_status(word):
    strs = ["Unexpected K-chars recieved",
                "Disparity errors recieved",
                "Not in table errors recieved"]
    ret = []
    if(word & 0x4):
        ret.append(strs[0])
    if(word & 0x2):
        ret.append(strs[1])
    if(word & 0x1):
        ret.append(strs[2])

    if not ret:
        ret = ["No errors"]
    return "\n".join(ret)

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--adc_a", action="store_true", help="send commands to ADC A")
    parser.add_argument("--adc_b", action="store_true", help="send commands to ADC B")
    parser.add_argument("--adc_c", action="store_true", help="send commands to ADC C")
    parser.add_argument("--adc_d", action="store_true", help="send commands to ADC D")
    parser.add_argument("--ti", action="store_true", help="Use commands for TI board")
    parser.add_argument("--port", type=int, default=4002, help="Port to connect to server at")


    args = parser.parse_args()

    devices = [SPI_Device.ADC_A if args.adc_a else None,
               SPI_Device.ADC_B if args.adc_b else None,
               SPI_Device.ADC_C if args.adc_c else None,
               SPI_Device.ADC_D if args.adc_d else None]\
                if not args.ti else [SPI_Device.TI_ADC]
    
    devices = [x for x in devices if x is not None]


    conn = connect_to_fpga(port=args.port)

    jesd_info_fn = "xil_jesd_regs.json"
    regs = read_reg_info_file(jesd_info_fn)
    results = {device:copy.deepcopy(regs) for device in devices}
    for device, these_regs in results.items():
        write_to_fpga(conn, device, 0x34, 0x1)
        for r in these_regs:
                read_reg(conn, device, r)

    print(results)

def calc_fchk(regs, lane):
    the_sum = 0
    for reg in regs[:6]:
        the_sum += reg.raw_value[lane]
    the_sum %= 255
    fchk = regs[6].fields[lane][0].value
    return the_sum, fchk
