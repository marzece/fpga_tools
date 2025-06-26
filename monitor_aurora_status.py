 # -*- coding: utf-8 -*-
import ceres_fpga_spi as fpga
from os import system
from time import sleep

#GOOD_SYMB = "✅"
#BAD_SYMB = "🟥"
GOOD_SYMB = "🟢"
BAD_SYMB = "🔴"


def send_command(conn, command):
    conn.sendall(command.encode("ascii"))
    return fpga.grab_response(conn)

def print_aurora_status(xems, ceres_status, fontus_valids, fontus_status):
    aurora_inner_lanes = fontus_status & 0xFFF
    aurora_veto_lanes = (fontus_status & 0xF000) >> 12
    aurora_inner_mult_valid = (fontus_status & 0x03F0000) >> 16
    aurora_inner_esum_valid = (fontus_status & 0xFC00000) >> 22
    aurora_veto_mult_valid = (fontus_status & 0x30000000) >> 28
    aurora_veto_esum_valid = (fontus_status & 0xC0000000) >> 30

    # The "lane up" signals aren't really that useful or reliable I think
    # Bits 0, 2, 4, etc are Multiplicity bits & 1,3,5 etc are ESUM bits
    mult_inner_lanes_up = [bool(aurora_inner_lanes & (1 << i)) for i in range(12) if (0x555 & (1 << i))]
    esum_inner_lanes_up = [bool(aurora_inner_lanes & (1 << i)) for i in range(12) if (0xAAA & (1 << i))]
    mult_veto_lanes_up = [bool(aurora_veto_lanes & 0x1), bool(aurora_veto_lanes & 0x40)]
    esum_veto_lanes_up = [bool(aurora_veto_lanes & 0x2), bool(aurora_veto_lanes & 0x80)]
    mult_lane_valid = [bool(fontus_valids & (1 << i)) for i in range(16) if (0x5555 & (1 << i))]
    esum_lane_valid = [bool(fontus_valids & (1 << i)) for i in range(16) if (0xAAAA & (1 << i))]

    inner_mult_trig_pipe_valid = [bool(aurora_inner_mult_valid & (0x1 << i)) for i in range(6)]
    inner_esum_trig_pipe_valid = [bool(aurora_inner_esum_valid & (0x1 << i)) for i in range(6)]
    veto_mult_trig_pipe_valid = [bool(aurora_veto_mult_valid & (0x1 << i)) for i in range(2)]
    veto_esum_trig_pipe_valid = [bool(aurora_veto_esum_valid & (0x1 << i)) for i in range(2)]

    mult_tp_valid = inner_mult_trig_pipe_valid + veto_mult_trig_pipe_valid
    esum_tp_valid = inner_esum_trig_pipe_valid + veto_esum_trig_pipe_valid

    print("\t         CERES ------------>  BP -> FONTUS ")
    for i, (xem, ceres_stat) in enumerate(zip(xems, ceres_status)):
        aurora_lane_up_0 = int(bool(ceres_stat & 0x1))
        aurora_lane_up_1 = bool(ceres_stat & 0x2)
        aurora_input_0 = bool(ceres_stat & 0x4)
        aurora_input_1 = bool(ceres_stat & 0x8)
        trig_cc_input_0 = bool(ceres_stat & 0x10)
        trig_cc_input_1 = bool(ceres_stat & 0x20)
        trig_dwidth_input_0 = bool(ceres_stat & 0x40)
        trig_dwidth_input_1 = bool(ceres_stat & 0x80)

        fontus_lane_index = xem - 4

        lane_0 = (trig_dwidth_input_0, trig_cc_input_0, aurora_input_0,
                  mult_lane_valid[fontus_lane_index], mult_tp_valid[fontus_lane_index])
        lane_1 = (trig_dwidth_input_1, trig_cc_input_1, aurora_input_1,
                  esum_lane_valid[fontus_lane_index], esum_tp_valid[fontus_lane_index])
        lane_0 = tuple([GOOD_SYMB if x else BAD_SYMB for x in lane_0])
        lane_1 = tuple([GOOD_SYMB if x else BAD_SYMB for x in lane_1])

        print("XEM %02i MULT Lane: %s -> %s -> %s -> %s ---> %s -> %s" % ((xem,) + lane_0) )
        print("XEM %02i ESUM Lane: %s -> %s -> %s -> %s ---> %s -> %s" % ((xem,) + lane_1) )


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--xem-mask", default=0xff0)
    parser.add_argument("--fontus_host", default="localhost")
    parser.add_argument("--ceres_host", default="localhost")
    args = parser.parse_args()

    xem_mask = args.xem_mask
    fontus_ip = args.fontus_host
    ceres_ip = args.ceres_host

    xems = []
    i = 0
    while xem_mask:
        if xem_mask & 1:
            xems.append(i)
        xem_mask >>= 1
        i += 1
    xem_mask = args.xem_mask

    fontus = fpga.connect_to_fpga(fontus_ip, 4002)
    ceres = fpga.connect_to_fpga(ceres_ip, 4003)
    _ = send_command(ceres, "set_active_xem_mask %u\r\n" % xem_mask)

    while(True):
        fontus_status = send_command(fontus, "read_aurora_status\r\n")
        fontus_valids = send_command(fontus, "read_aurora_lane_up_status\r\n")
        ceres_status = send_command(ceres, "read_aurora_status\r\n")

        system("clear")
        print_aurora_status(xems, ceres_status, fontus_valids, fontus_status)
        sleep(5)
