import hiredis
import redis
from time import sleep, time
import socket
import numpy as np
from collections import namedtuple, defaultdict

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="localhost", type=str, help="IP of Kintex command server")
    parser.add_argument("--port", default=4002, type=int, help="Port of Kintex command server")
    args = parser.parse_args()
    fd = socket.socket(family=socket.AF_INET, type=socket.SOCK_STREAM)
    try:
        fd.connect((args.ip, args.port))
    except OSError:
        print("Given IP address is not valid")
        exit()
    except ConnectionRefusedError:
        print("Socket connection at %s:%i is not available" % (args.ip, args.port))
        exit()

    reader = hiredis.Reader()
    redis_db = redis.Redis()
    Result = namedtuple("Result", ("time", "vals"))
    prev_data = defaultdict(list)
    while(True):
        data = defaultdict(list)
        for channel in [0, 1]:
            t = time()
            fd.sendall(b"read_gpio1 %i\r\n" % (int(channel),))
            reader.feed(fd.recv(1024)) # !TODO add time out to this
            result = reader.gets()
            #result = np.random.randint(low=0, high=500, size=4)
            #print(t, channel, result)
            if type(result) == hiredis.ReplyError:
                print("Error response from server: %s" % str(result))
                continue

            data[channel] = Result(t, result)
            if(prev_data and prev_data[channel]):
                dt = data[channel].time - prev_data[channel].time
                if(dt <= 0):
                    # HMMM how the heck did this happen
                    print("negative or zero time found....not sure what happened")
                    continue
                dv = (data[channel].vals - prev_data[channel].vals)/dt
                dv /= 250e6
                if(dv < 0):
                    # You'll get negative values if the JESD core was reset
                    print("negative error count found...probably a reset")
                    prev_data[channel] = data[channel]
                    continue
                else:
                    #redis_db.publish("jesd_sync", "{} {} {}".format(channel, *dv))
                    print("jesd_sync", "{} {}".format(channel, round(dv, 3)))
            prev_data[channel] = data[channel]
        sleep(2)
