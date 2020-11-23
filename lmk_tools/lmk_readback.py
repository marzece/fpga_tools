from lmk import LMK
import socket
from time import sleep
from fpga_spi import connect_to_fpga, lmk_spi
from lmk import LMK

if __name__ == "__main__":
    conn = connect_to_fpga()
    lmk = LMK("MASADA_ADC_Config.cfg")
    for reg_addr, reg in lmk.reg_info.items():
        mode = reg["Mode"]
        if(mode is not None and "R" in mode.upper()):
            rb = lmk_spi(conn, 0x1, reg_addr, 0)
            print("Reg {} => {}".format(hex(reg_addr), hex(rb[-1])))
        else:
            print("Reg {} is not readable".format(hex(reg_addr)))

