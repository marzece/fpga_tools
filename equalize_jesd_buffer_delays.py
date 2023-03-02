import socket
import hiredis
from time import sleep
from ceres_fpga_spi import connect_to_fpga, grab_response, SPI_Device

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--adc_a", action="store_true", help="send commands to ADC A")
    parser.add_argument("--adc_b", action="store_true", help="send commands to ADC B")
    parser.add_argument("--adc_c", action="store_true", help="send commands to ADC C")
    parser.add_argument("--adc_d", action="store_true", help="send commands to ADC D")
    parser.add_argument("--xem-mask", type=str, default=None, help="Mask to indicate which XEMs should recieve programming, default is none.")
    parser.add_argument("--target", type=int, default=40, help="Buffer delay value that this script will ensure every ADC has")
    parser.add_argument("--port", type=int, default=4002, help="Port to connect to server at")

    args = parser.parse_args()

    target = args.target
    devices = [SPI_Device.ADC_A if args.adc_a else None,
               SPI_Device.ADC_B if args.adc_b else None,
               SPI_Device.ADC_C if args.adc_c else None,
               SPI_Device.ADC_D if args.adc_d else None]

    devices = [x for x in devices if x is not None]

    conn = connect_to_fpga(port=args.port)

    xem_mask = args.xem_mask
    if(xem_mask is None):
        print("Must set XEM Mask!")
        exit(1)
    xem_mask = int(xem_mask, base=0)


    # First get all the initial buffer delays
    loop_count = 0
    while(xem_mask != 0):
        xems = [i for i in range(32) if (xem_mask & (1<<i))]
        delays = {}
        conn.sendall(("set_active_xem_mask %i\r\n" % xem_mask).encode("ascii"))
        resp = grab_response(conn)
        if(type(resp) == hiredis.ReplyError):
            print("Error while setting XEM Mask: %s" % str(resp))
            exit(1)

        delays = {xem_id:{} for xem_id in xems}
        for i, device in enumerate(devices):
            if device is None:
                continue
            conn.sendall(("read_jesd_rx_lane_buffer_adj %i\r\n" %  i).encode('ascii'))
            resp = grab_response(conn)

            for xem_id, buf_resp in zip(xems, resp):
                delays[xem_id][i] = min(buf_resp)

        # Now check which XEMs are not at the target
        good_xems = []
        for xem_id in xems:
            if(all([buf_len == target for buf_len in delays[xem_id].values()])):
                good_xems.append(xem_id)
        [xems.remove(x) for x in good_xems]
        xem_mask = sum([1<<x for x in xems])

        print(loop_count, xems)
        loop_count += 1

        # Now do the equalizing
        #for (xem_id, lane), current_buf in delays.items():
        for xem_id in xems:
            conn.sendall(("set_active_xem_mask %i\r\n" % (1<<xem_id)).encode("ascii"))
            resp = grab_response(conn)
            if(type(resp) == hiredis.ReplyError):
                print("Error while setting XEM Mask: %s" % str(resp))
                exit(1)

            for lane, _ in enumerate(devices):
                conn.sendall(("read_jesd_buffer_delay %i\r\n" % lane).encode('ascii'))
                resp = grab_response(conn)
                if(type(resp) == hiredis.ReplyError):
                    print("Error while getting buffer delay" % str(resp))
                    exit(1)
        

                current_buf = delays[xem_id][lane]
                current_buf_setting = resp[0]

                print(xem_id, lane, current_buf, target)
                adjustment = current_buf - target
                # If we're already at our target, no need to touch anything
                if(adjustment == 0):
                    continue
                buffer_value = current_buf_setting + adjustment
                conn.sendall(("write_jesd_buffer_delay %i %i\r\n" % (lane, buffer_value)).encode('ascii'))
                resp = grab_response(conn)
                if(type(resp) == hiredis.ReplyError):
                    print("Error while setting buffer delay" % str(resp))
                    exit(1)

        # Once the settings are set, need to reset the JESD lanes for the effect to take place
        conn.sendall(("set_active_xem_mask %i\r\n" % xem_mask).encode("ascii"))
        resp = grab_response(conn)
        if(type(resp) == hiredis.ReplyError):
            print("Error while doing a setting XEM mask" % str(resp))
            exit(1)

        conn.sendall("jesd_sys_reset\r\n".encode('ascii'))
        resp = grab_response(conn)
        if(type(resp) == hiredis.ReplyError):
            print("Error while doing a JESD reset" % str(resp))
            exit(1)
        sleep(1)
        if(loop_count > 10):
            break
