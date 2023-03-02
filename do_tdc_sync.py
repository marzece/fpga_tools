from __future__ import division
import socket
import os
import hiredis
from time import sleep
from collections import Counter
from math import cos, sin, atan2, pi
from functools import reduce

def connect_to_fpga(ip="localhost", port=4002):
    fpga_conn = socket.create_connection((ip, port))
    return fpga_conn

_resp_reader = hiredis.Reader()
def send_command(server, command):
    command = "%s\r\n" % command
    command = command.encode("ascii")
    server.sendall(command)
    _resp_reader.feed(server.recv(1024))
    resp = _resp_reader.gets()
    if(type(resp) == hiredis.ReplyError):
        print("ERROR: %s" % str(resp))
        os._exit(os.EX_DATAERR)
    return resp

def average_phases(counts):
    # Average is a bit weird for phase angles since  0 & 1 are the same point
    # so instead convert each phase to polar coordinates, than average those
    #coordinates, then convert back to a phase.
    n_items = sum(counts.values())
    x = sum([cos(2*pi*val)*count/n_items for val, count in counts.items()])
    y = sum([sin(2*pi*val)*count/n_items for val, count in counts.items()])
    avg = atan2(y,x)
    return avg/(2*pi)

def phase_to_fom(phase):
    return abs(abs(phase) -0.5)

def binary_search(xems):
    """ This is supposed to do a binary search for the best clock for each board"""
    return NotImplemented
    epsilon = 0.05
    xems = sorted(xems)
    xem_mask = reduce(lambda x,y: x|y, xems)
    # First do coarse search of 8 evenly space points
    summed_phase = 0

    increment = 360e3/8
    results = {}
    phases = []
    for i in range(8):
        data = []
        for _ in range(DWELL_ITERATIONS):
            send_command(fontus_conn, "do_sync 200")
            data.append([tdc_map[int(x)] for x in send_command(ceres_conn, "read_tdc_value")])
            sleep(0.2)
        results[summed_phase] = {xem_id:average_phases(Counter([x[i] for x in data])) for i, xem_id in enumerate(xems)}
        send_command(ceres_conn, ("increment_clock_wiz_phase %i" % increment))
        phases.append(summed_phase)
        summed_phase += increment

    step_size = increment/2.
    # Here after I need to address each XEM individually, since I need to increment
    # each boards clock differently
    for xem_id in xem_ids:
        # extract the results for this specific XEM
        data = [(angle, results[angle][xem_id]) for angle in phases]

        # First identify the best octant for searching
        best_angle, best_fom = min(data, key=lambda x: phase_to_fom(x[1]))
        best_fom = phase_to_fom(best_fom)

        send_command(ceres_conn, ("increment_clock_wiz_phase %i" % best_angle))
        send_command(ceres_conn, ("set_active_xem_mask %i" % (1<<xem_id)))
        current_phase = best_angle
        while(best_fom < epsilon):
            # ????
            break
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--fontus", type=str, default="localhost:4002")
    parser.add_argument("--ceres", type=str, default="localhost:4003")
    parser.add_argument("--xem-mask", type=lambda x: int(x,base=0), default=0xFF0)


    args = parser.parse_args()
    fontus_ip = args.fontus.split(':')
    ceres_ip = args.ceres.split(':')
    xem_mask = args.xem_mask

    xem_ids = [i for i in range(32) if (xem_mask & (1<<i)) !=0]


    tdc_map = {0:(0.75+1.0)/2,
               8:(0.5+0.75)/2,
               12:(0.25+0.5)/2,
               14: (0.0+0.25)/2}


    fontus_port = 4002
    if(len(fontus_ip) > 1):
        fontus_ip, fontus_port = fontus_ip[0], int(fontus_ip[1])

    ceres_port = 4003
    if(len(ceres_ip) > 1):
        ceres_ip, ceres_port = ceres_ip[0], int(ceres_ip[1])

    print(fontus_ip, fontus_port)
    print(ceres_ip, ceres_port)
    fontus_conn = connect_to_fpga(fontus_ip, fontus_port)
    ceres_conn = connect_to_fpga(ceres_ip, ceres_port)

    send_command(ceres_conn, ("set_active_xem_mask %i" % xem_mask))




    INCREMENT = 10000
    DWELL_ITERATIONS = 100
    summed_phase = 0
    results = {}

    send_command(ceres_conn, "write_lmk_spi 2 0 0x139 0x0")
    sleep(0.1)
    send_command(ceres_conn, "write_lmk_spi 2 0 0x143 0x51")
    sleep(0.1)
    send_command(ceres_conn, "write_lmk_spi 2 0 0x144 0x0")
    sleep(0.1)
    send_command(fontus_conn, "do_sync 200")
    sleep(0.1)
    send_command(ceres_conn, "write_lmk_spi 2 0 0x139 0x3")
    sleep(0.1)
    send_command(ceres_conn, "write_lmk_spi 2 0 0x143 0x0")
    sleep(0.1)
    send_command(ceres_conn, "write_lmk_spi 2 0 0x144 0xFF")
    sleep(0.1)


    # TODO instead of doing a fixed scan I could do some sort of binary
    # search.
    while summed_phase < 360e3:
        data = []
        print("Phase offset = %0.2f degrees" % (summed_phase/1e3))
        for _ in range(DWELL_ITERATIONS):
        #while True:
            send_command(fontus_conn, "do_sync 200")
            try:
                data.append([tdc_map[int(x)] for x in send_command(ceres_conn, "read_tdc_value")])
            except KeyError:
                pass
            sleep(0.1)
        #avgs = [sum([x[i] for x in data])/DWELL_ITERATIONS for i in range(len(data[0]))]
        results[summed_phase] = [Counter([x[i] for x in data]) for i in range(len(data[0]))]

        send_command(ceres_conn, ("increment_clock_wiz_phase %i" % INCREMENT))
        summed_phase += INCREMENT

# If 360-degrees isn't divisible by the step size, then keep track of how far off we are
phase_offset = summed_phase-360e3

ks = sorted(results.keys())
boards = {xem_id:{x:results[x][i] for x in ks} for i,xem_id in enumerate(xem_ids)}
temp = {}
for xem_id, board in boards.items():
    phase_avgs = {}
    for k,v in board.items():
        phase_avgs[k] = average_phases(v)
    temp[xem_id] = phase_avgs
boards = temp
    
# Now find the best angle for each board
best_angles = {}
for xem_id, board in boards.items():
    best_angles[xem_id] = min(board.items(), key=lambda x: phase_to_fom(x[1]))
print("Best Angles = ", best_angles)

# Finally set the clocks to their best angle
for xem_id, (angle, fom) in best_angles.items():
    send_command(ceres_conn, "set_active_xem_mask %i" % (1<<xem_id))
    send_command(ceres_conn, "increment_clock_wiz_phase %i" % (angle-phase_offset))
