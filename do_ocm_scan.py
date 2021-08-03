from fpga_spi import connect_to_local_client, grab_response
from multiprocessing import Process
import subprocess
import os
import numpy as np


fpga_conn = connect_to_local_client()

def ocm_command(value):
    #cmd = "set_ocm_for_all_channels 0x%x\r\n" % value
    cmd = "set_ocm_form_channel 1 0x%x\r\n" % value
    return cmd.encode("ascii")

def run_data_builder():
    cmd = "jesd_sys_reset\n".encode("ascii")
    fpga_conn[0].write(cmd)
    _ = grab_response(fpga_conn)
    subprocess.run(["/Users/ericmarzec/fpga_tools/fakernet_code/fakernet_data_builder", "--num", "1"])
    # Just gonna assume the above ran well

def activate_trigger():
    fpga_conn[0].write("write_data_pipeline_local_trigger_enable 1\n".encode('ascii'))
    _ = grab_response(fpga_conn)

def deactivate_trigger():
    fpga_conn[0].write("write_data_pipeline_local_trigger_enable 0\n".encode('ascii'))
    _ = grab_response(fpga_conn)

def process_data(data_buffer):
    NUM_CHANNELS = 8
    wf_dtype = [("header", ">u4", 5), ("data", ">u2", (2000+4)*NUM_CHANNELS)]
    data = np.copy(np.frombuffer(data_buffer, wf_dtype))
    data['data'][:, 0::2004] = data['data'][:, 2::2004]
    data['data'][:, 1::2004] = data['data'][:, 3::2004]

    data['data'][:, 2003::2004] = data['data'][:, 2000::2004]
    data['data'][:, 2002::2004] = data['data'][:, 2001::2004]
    data = data['data'].reshape((data['data'].shape[0], NUM_CHANNELS, 2004))
    
    return data

def evaluate_noise():
    db_process = Process(target=run_data_builder)
    try:
        os.remove("fpga_data.dat")
    except FileNotFoundError:
        pass
    db_process.start()

    activate_trigger()
    db_process.join()
    deactivate_trigger()

    with open("fpga_data.dat", "rb") as f:
        data_buffer = f.read()
        return process_data(data_buffer)

if __name__ == "__main__":
    # Pretty close to 10mV
    increment = 0x01
    bottom = 0xb852 # ~1.8V
    top = 0xE148 # ~2.2V

    #fpga_conn[0].write("set_trigger_mode 0".encode('ascii'))
    #_ = grab_response(fpga_conn)
    #fpga_conn[0].write("set_bias_for_all_channels 0x6666".encode('ascii'))
    #_ = grab_response(fpga_conn)

    results = {}
    for ocm in range(bottom, top, increment):
        print("OCM = 0x%x" % ocm)
        fpga_conn[0].write(ocm_command(ocm))
        _ = grab_response(fpga_conn)

        results[ocm] = evaluate_noise();

    keys = np.array(list(results.keys()))
    values = np.array(list(results.values()))
    np.savez("ocm_scan_results.npz", ocms=keys, results=values)
