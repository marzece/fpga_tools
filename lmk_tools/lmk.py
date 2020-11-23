import re
import json
from fpga_spi import connect_to_fpga, lmk_spi

# Helper function for getting certain bits from a word
def _grab_bits_(word, first_bit, last_bit):
    mask = 0
    # I'm sure there's a way to do this without looping
    for _ in range(last_bit - first_bit + 1):
        mask = mask << 1
        mask |= 1
    return (word >> first_bit) & mask

class Field:
    def __init__(self, **kwargs):
        self.name = kwargs['name']
        self.reg_address = kwargs['address']
        self.bit_low = kwargs['bit_low']
        self.bit_high = kwargs['bit_high']
        self.description = kwargs.get('description')
        self.translation = kwargs.get('translation')
        self.reset_value = kwargs.get('default_value')
        self.rw = kwargs.get('mode')
        self.value = kwargs.get("value")
        if not self.translation:
            self.translation = lambda x: x
        else:
            self.translation = eval(self.translation)
    def __repr__(self):
        try:
            reg_string = hex(self.reg_address)
        except TypeError:
            reg_string = ", ".join([hex(x) for x in self.reg_address])

        ret = "{}({}[{}:{}])=>{}".format(self.name, reg_string, self.bit_low, self.bit_high, hex(self.value))
        return ret

class Clock:
    def __init__(self, **kwargs):
        self.freq = kwargs.get("freq")
        self.name = kwargs.get("name")
    def __repr__(self):
        return "<" + self.name + "-" + str(self.freq) + " MHz>"
    def __eq__(self, lhs):
        return hash(self) == hash(lhs)

class LMK:
    def __init__(self, config_fn):
        self.reg_info = {}
        with open("ti_lmk_regs.json") as reg_file:
            reg_json = json.load(reg_file)
            regs_list = reg_json["Registers"]
            for reg in regs_list:
                self.reg_info[int(reg["Address"], 16)] = reg
        # TODO add something to go over the reg_info and convert strings
        # to ints and maybe other stuff

        if not config_fn:
            return

        vco1 = Clock(name="VCO0", freq=2500)
        vco0 = Clock(name="VCO1", freq=3000)
        ext_vco = Clock(name="OSCin", freq=122.88)
        # TODO add ability to add more external clocks
        self.clocks = [vco0, vco1, ext_vco]
        self.read_config_file(config_fn)

        # Considered using a default dict but I think I want errors for trying
        # to access unknown keys.
        self.fields = {}
        for reg in self.config.keys():
            value = self.config[reg]
            info = self.reg_info[reg]
            # Make sure the reg is writable
            if("w" not in info["Mode"].lower()):
                print("Reg 0x%x not writable!" % reg)

            for field in info["Fields"]:
                field_start_bit = int(field["Start_Bit"])
                field_end_bit = int(field["End_Bit"])
                field_value = _grab_bits_(value, field_start_bit, field_end_bit)

                # The field name looks something like BLAH[3:0] so I wanna strip
                # off everything after (and including) the '['
                # BUT(!) if there's two '[x:y]' fields, then it's a concatenated
                # field that stretches across 2 or more registers
                # The LAST (and usually only) square bracket gives the 
                # "local bits" (for this register).
                # The first square brackets (if it exists) gives the location
                # of the bits in the full word e.g. if the "global" bits are
                # [7:0], the that register is the LSB of the full word.
                #
                # So, the algorithm here is first strip off the last '[xx]'
                # then check if there still exists a '[]', if so strip
                # that off save the bit values.
                field_name = field["Field_Name"]
                field_name = field_name[:field_name.rfind('[')]
                if(field_name.find('[') == -1):
                    #self.fields[field_name] = field_value
                    new_field = Field(mode=info["Mode"], name=field_name, address=reg, bit_high=field_end_bit,
                                      bit_low=field_start_bit, description=field["Field_Description"],
                                      value=field_value)
                    self.fields[field_name] = new_field
                    continue

                # If here it's a concatenated word
                # TODO used regex instead of all this mucking about.
                # Grab the bits, then grab the proper field name
                bit_str = field_name[field_name.find('['):]
                field_name = field_name[:field_name.rfind('[')]

                bit_high = int(bit_str[1:bit_str.find(":")])
                bit_low = int(bit_str[bit_str.find(":")+1:-1])

                # First check if other bits of this field have already been
                # added to the fields dict. If not add these bits in,
                # if they have AND these bits in (at the proper location)
                try:
                    new_field = self.fields[field_name]
                except KeyError:
                    new_field = Field(mode=info["Mode"], name=field_name, address=[], bit_high=field_end_bit,
                                  bit_low=field_start_bit, description=field["Field_Description"],
                                  value=0)
                    self.fields[field_name] = new_field

                self.fields[field_name].value |= field_value << bit_low
                self.fields[field_name].reg_address.append(reg)


    def read_config_file(self, fn):
        self.reg_dict = {}
        with open(fn) as f:
            lines = list(f)
            regex = r"^0x(\d|A|B|C|D|E|F)+ 0x(\d|A|B|C|D|E|F)+$"
            lines = filter(lambda x: re.match(regex, x), lines)
            lines = [[int(x, 16) for x in line.split(' ')] for line in lines]
        # TODO add someting here for registers that are set multiple times
        self.config = dict(lines)

    def readback_config(self):
        conn = connect_to_fpga()
        lmk = LMK("MASADA_ADC_Config.cfg")
        for reg_addr, reg in lmk.reg_info.items():
            mode = reg["Mode"]
            if("R" not in mode.upper()):
                continue

            rb = lmk_spi(conn, 0x1, reg_addr, 0)
            print("Reg {} => {}".format(hex(reg_addr), hex(rb[-1])))
