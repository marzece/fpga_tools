import socket
import argparse
import hiredis
from time import sleep
from collections import defaultdict, namedtuple
from ceres_fpga_spi import adc_spi, connect_to_fpga, SPI_Device


dig_pages = {"interleaving": 0x6100, "decimation": 0x6141, "main_dig": 0x6800,
        "jesd_dig": 0x6900, "jesd_analog": 0x6A00}

analog_pages = {"master" : 0x80, "adc": 0xF}

general_pages  = {"general": 0x0}

regs = {"interleaving": [0x18, 0x68],
        "decimation": [0x0, 0x1, 0x2],
        "main_dig": [0x0, 0x42, 0x4E, 0xAB, 0xAD, 0xF7],
        "jesd_dig": [0x0, 0x1, 0x2, 0x3, 0x5, 0x6, 0x19, 0x1A, 0x1B, 0x1C,
                         0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22],
        "jesd_analog": [0x12, 0x13, 0x16, 0x1B],
        "master": [0x20, 0x21, 0x23, 0x24, 0x26, 0x3A, 0x39, 0x53, 0x55, 0x56, 0x59],
        "adc": [0x5F, 0x60, 0x61, 0x6C, 0x6D, 0x74, 0x75, 0x76, 0x77, 0x78],
        "general": [0x1, 0x3, 0x4, 0x11] }

adc_regs = {"general": general_pages, "analog": analog_pages, "digital": dig_pages}
for ad in adc_regs.keys():
    for page, page_addr in adc_regs[ad].items():
        adc_regs[ad][page] = {"page_addr":page_addr, "regs":regs[page]}


def write_page_addr(conn, this_adc, ad, page_addr):
    if(ad == "analog"):
        #spi_command(conn, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x0, 0x11, page_addr))
        adc_spi(conn, this_adc, 0x0, 0x11, page_addr)
    elif(ad == "general"):
        pass
    else:
        page_addr1 = page_addr & 0xFF
        page_addr2 = (page_addr & 0xFF00) >> 8
        adc_spi(conn, this_adc, 0x4, 0x3, page_addr1)
        adc_spi(conn, this_adc, 0x4, 0x4, page_addr2)
        #spi_command(conn, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x4, 0x3, page_addr1))
        #spi_command(conn, "write_adc_spi 0x%x 0x%x 0x%x\n" % (0x4, 0x4, page_addr2))

def read_reg(conn, this_adc, ad, reg):
    # This functions assumes the page address has been written to the
    # page selection address
    values = None
    if(ad == "analog" or ad == "general"):
        values = adc_spi(conn, this_adc, 0x8, reg, 0x0)
    else:
        values = adc_spi(conn, this_adc, 0xE, reg, 0x0)
    return values[-1]

def adc_readback(conn, adcs):
    for this_adc in adcs:
        for ad, pages in adc_regs.items():
            for page_name, page_info in  pages.items():
                page_addr = page_info['page_addr']
                regs = page_info['regs']
                write_page_addr(conn, this_adc, ad, page_addr)
                for reg in regs:
                    v = read_reg(conn, this_adc, ad, reg)
                    print("%s(0x%X) 0x%X 0x%X" % (page_name, page_addr, reg, v))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--adc_a", action="store_true", help="send commands to ADC A")
    parser.add_argument("--adc_b", action="store_true", help="send commands to ADC B")
    parser.add_argument("--adc_c", action="store_true", help="send commands to ADC C")
    parser.add_argument("--adc_d", action="store_true", help="send commands to ADC D")
    parser.add_argument("xem", type=int, help="which xem to talk to")
    parser.add_argument("--port", type=int, default=4002, help="Port to connect to server on")
    args = parser.parse_args()

    adcs = [SPI_Device.ADC_A if args.adc_a else None,
            SPI_Device.ADC_B if args.adc_b else None,
            SPI_Device.ADC_C if args.adc_c else None,
            SPI_Device.ADC_D if args.adc_d else None]

    adcs = [x for x in adcs if x is not None]
    if not adcs:
        print("Must specify if you want to send to ADC A or B")
        exit()
    reader = hiredis.Reader()

    fpga_conn = connect_to_fpga(port=args.port)

    mask = (1<<args.xem)
    fpga_conn.sendall(("set_active_xem_mask %i\r\n" % mask).encode("ascii"))
    reader.feed(fpga_conn.recv(1024))
    resp = reader.gets()
    if(type(resp) == hiredis.ReplyError):
        print("Error %s" % str(resp))
        exit()

    adc_readback(fpga_conn, adcs)
