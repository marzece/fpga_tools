import socket

import numpy as np
import argparse
from time import sleep
from collections import defaultdict
from fpga_spi import adc_spi, lmk_spi, grab_response, connect_to_local_client, SPI_Device
from readback_config import write_page_addr, read_reg
from configure_adc_and_clock import parse_config_file, do_programming


def replace_instructions(instructions, replacements):
    new_instructions = instructions.copy()
    for addr, new_value in replacements:
        # TODO this is ADS only...maybe someday wanna do this for LMK stuff too
        index = [i for i, (chip, _addr, _) in enumerate(new_instructions) if 
                                                    chip=="ADS" and _addr==addr]
        if( len(index)!= 1):
            # Just toss an error, I aint got time for this
            raise Exception("Make sure the instructions have the desired replacement address in them dummkopf")
        index = index[0]
        old_val = new_instructions[index]
        new_line = (old_val[0], old_val[1], new_value)
        new_instructions[index] = new_line
    return new_instructions

def do_jesd_reset(server):
    server[0].write("jesd_sys_reset\n".encode("ascii"))
    return grab_response(server)

def check_jesd_synced(server, which_adc):
    if(which_adc == SPI_Device.ADC_A):
        server[0].write("jesd_a_is_synced\n".encode("ascii"))
    elif(which_adc == SPI_Device.ADC_B):
        server[0].write("jesd_b_is_synced\n".encode("ascii"))
    return grab_response(server)

def setup_jesd_error_stuff(server, which_adc):
    JESD_ERROR_REPORTING_OFFSET = 0x34
    # 0x1 enables the error counters
    # 0x8 disables error reporting with sync
    error_reporting_value = 0x1 | 0x8
    command = "jesd_a_write" if which_adc == SPI_Device.ADC_A else "jesd_b_write"
    command = "%s 0x%x 0x%x\n" % (command, JESD_ERROR_REPORTING_OFFSET, error_reporting_value)
    server[0].write(command.encode("ascii"))
    return grab_response(server)

def check_error_rates(server, adcs):
    count = 5
    #err_rates = np.zeros(1, [("A", np.float32, 4), ("B", np.float32, 4), ("SYNCA", np.float32), ("SYNCB", np.float32)])
    err_rates = np.zeros(1, [("A", np.float32, 4), ("SYNCA", np.float32)])
    for _ in range(count):
        # First collect the error rates
        fpga_conn[0].write("read_all_error_rates\r\n".encode("ascii"))
        err_rates.view(np.float32)[:] = np.array(grab_response(fpga_conn))

    err_rates.view(np.float32)[:] /= float(count)
    return err_rates

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--adc_a", action="store_true", help="send commands to ADC A")
    parser.add_argument("--adc_b", action="store_true", help="send commands to ADC B")
    parser.add_argument("--adc_c", action="store_true", help="send commands to ADC C")
    parser.add_argument("--adc_d", action="store_true", help="send commands to ADC D")
    parser.add_argument("--ti", action="store_true", help="send commands to TI/MASADA ADC ")

    args = parser.parse_args()

    fpga_conn = connect_to_local_client()
    if not args.ti:
        adcs = [SPI_Device.ADC_A if args.adc_a else None,
                SPI_Device.ADC_B if args.adc_b else None,
                SPI_Device.ADC_C if args.adc_c else None,
                SPI_Device.ADC_D if args.adc_d else None]
    else:
        adcs = [SPI_Device.TI_ADC]

    #adcs = [x for x in adcs if x is not None]
    if not any(adcs):
        print("Must specify if you want to send to ADC A or B")
        exit()

    # TODO, don't hardcode this
    fn = "lmk_tools/HERMES_Config.cfg"
    if args.ti:
        fn = "lmk_tools/MASADA_Config.cfg"

    with open(fn, 'r') as f:
        instructions = parse_config_file(f)

    valid_emphasis_values  = [x<<2 for x in [0, 1, 3, 7, 15, 31, 63]]
    emphasis_addr = [0x6A0012, 0x6A0013]
    swing_addr = 0x6A001B
    valid_swing_values  = [x<<5 for x in [0, 1, 2, 3, 4, 5, 6, 7]]
    #valid_values = {0x6A0012: valid_emphasis_values, 0x6A0013: valid_emphasis_values,
    #                0x6A001B: valid_swing_values}

    error_rates = {}

    for adc in adcs:
        if adc is None:
            continue
        setup_jesd_error_stuff(fpga_conn, adc)
    setup_jesd_error_stuff(fpga_conn, SPI_Device.ADC_B)

    print("Main loop")
    for swing in valid_swing_values:
        for emph_a in valid_emphasis_values:
            for emph_b in valid_emphasis_values:
            ## LOOP OVER ALL VALUES
                print("Swing = ", swing)
                print("emph_a = ", emph_a)
                print("emph_b = ", emph_b)
                key = (swing, emph_a, emph_b)
                new_instructions = replace_instructions(instructions, [(emphasis_addr[0], emph_a),
                                                                       (emphasis_addr[1], emph_b),
                                                                       (swing_addr, swing)])
                do_programming(fpga_conn, adcs, new_instructions)
                do_jesd_reset(fpga_conn)

                error_rates[key] = check_error_rates(fpga_conn, adcs)

                if(check_jesd_synced(fpga_conn, SPI_Device.ADC_A) == 0):
                    error_rates[key]["A"][:] = np.inf
                    error_rates[key]["SYNCA"] = np.inf
                    print("A not synced")

                if(check_jesd_synced(fpga_conn, SPI_Device.ADC_B) == 0):
                    error_rates[key]["B"][:] = np.inf
                    error_rates[key]["SYNCB"] = np.inf
                    print("B not synced")

    with open("scan_data_out_new_suppy.dat", "w") as fout:
        fout.write(str(error_rates))
