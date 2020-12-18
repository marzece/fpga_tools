import json
import socket
from fpga_spi import connect_to_local_client, decode_data

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


def write_to_fpga(conn, addr, value):
    command = "jesd_write 0x%x 0x%x\n" % (addr, value)
    conn[0].write(command.encode("ascii"))
    data = decode_data(conn[1].read_line())
    return data
def read_from_fpga(conn, addr):
    command = "jesd_read 0x%x\n" % addr
    conn.sendall(command.encode("ascii"))
    data = decode_data(conn[1].read_line())
    return data

def read_reg(conn, reg):
    if(reg.per_lane_reg):
        for fields, addr in zip(reg.fields, reg.addr):
            word = read_from_fpga(conn, addr)
            for f in fields:
                f.get_value(word)
    else:
        word = read_from_fpga(conn, reg.addr)
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
    host = "192.168.1.10"
    port = 4001
    #conn = socket.create_connection((host, port))
    conn = connect_to_local_client()

    jesd_info_fn = "xil_jesd_regs.json"
    regs = read_reg_info_file(jesd_info_fn)
    write_to_fpga(conn, 0x34, 0x1)
    for r in regs:
        read_reg(conn, r)


