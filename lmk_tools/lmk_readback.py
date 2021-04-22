from lmk import LMK
from time import sleep
from fpga_spi import connect_to_local_client, spi_command, SPI_Device
from lmk import LMK

if __name__ == "__main__":
    conn = connect_to_local_client()
    lmk = LMK("HERMES_CONFIG.cfg")
    for reg_addr, reg in lmk.reg_info.items():
        mode = reg["Mode"]
        if(mode is not None and "R" in mode.upper()):
            rb = spi_command(conn, SPI_Device.LMK, 0x1, reg_addr, 0)
            print("Reg {} => {}".format(hex(reg_addr), hex(rb[-1])))
        else:
            print("Reg {} is not readable".format(hex(reg_addr)))

