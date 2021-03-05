from fpga_spi import connect_to_local_client, grab_response
from multiprocessing import Process
import subprocess
import os
import numpy as np


fpga_conn = connect_to_local_client()

def bias_command(value):
    cmd = "set_bias_for_all_channels 0x%x\r\n" % value
    return cmd.encode("ascii")

def run_data_builder():
    cmd = "jesd_sys_reset".encode("ascii")
    fpga_conn[0].write(cmd)
    _ = grab_response(fpga_conn)
    subprocess.run(["/Users/ericmarzec/fpga_tools/fakernet_code/fakernet_data_builder", "--num", "100"])
    # Just gonna assume the above ran well


def activate_trigger():
    fpga_conn[0].write("set_activate_trigger 1\n".encode('ascii'))
    _ = grab_response(fpga_conn)

def deactivate_trigger():
    fpga_conn[0].write("set_activate_trigger 0\n".encode('ascii'))
    _ = grab_response(fpga_conn)

def process_data(data_buffer):
    NUM_CHANNELS = 8
    wf_dtype = [("header", ">u4", 5), ("data", ">u2", (4000+2)*NUM_CHANNELS)]
    data = np.copy(np.frombuffer(data_buffer, wf_dtype))
    data['data'][:, 0::4002] = data['data'][:, 2::4002]
    data['data'][:, 1::4002] = data['data'][:, 3::4002]
    #data["data"] = data["data"] >> 2
    data = data['data'].reshape((data['data'].shape[0], NUM_CHANNELS, 4002))
    
    #rms = np.sqrt(np.mean((data - np.meandata)**2))
    #means = np.mean(data, axis=(0, 2))
    #rms = np.std(data, axis=(0,2))
    #diffs = np.min(np.abs(np.diff(data.astype(float), axis=2)), axis=2)
    return data
    #return (np.dstack([means, rms]),
    #        np.dstack([np.mean(diffs, axis=0), np.std(diffs, axis=0)]),
    #        data)

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
    bias_increment = 0x87

    fpga_conn[0].write("set_trigger_mode 0".encode('ascii'))
    _ = grab_response(fpga_conn)
    results = {}
    for bias in range(0x3333, 0x7777, bias_increment):
    #for bias in range(0xAE14, 0xEB85, bias_increment):
        print("BIAS = 0x%x" % bias)
        fpga_conn[0].write(bias_command(bias))
        _ = grab_response(fpga_conn)

        results[bias] = evaluate_noise();

    keys = np.array(list(results.keys()))
    values = np.array(list(results.values()))
    np.savez("bias_scan_results.npz", biases=keys, results=values)
