import hiredis
import redis
from time import sleep, time
import socket
import numpy as np
from collections import namedtuple, defaultdict

if __name__ == "__main__":
    import argparse
    fout = open("poll_result.dat", 'w', buffering=1)
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="localhost", type=str, help="IP of Kintex command server")
    parser.add_argument("--port", default=4002, type=int, help="Port of Kintex command server")
    parser.add_argument("--jesds", default=[0,1,2,3], nargs='+',  help="Which JESD streams should be monitored")
    args = parser.parse_args()
    fd = socket.socket(family=socket.AF_INET, type=socket.SOCK_STREAM)
    print(args)
    jesds = [int(x) for x in args.jesds]
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
        for lane in jesds:
            t = time()
            fd.sendall(b"jesd_error_count %i\r\n" % (int(lane),))
            reader.feed(fd.recv(1024)) # !TODO add time out to this
            result = reader.gets()
            #result = np.random.randint(low=0, high=500, size=4)
            print(t, lane, result)
            fout.write("%s %s %s\n" % (str(t), str(lane), str(result)))
            if type(result) == hiredis.ReplyError:
                print("Error response from server: %s" % str(result))
                continue

            data[lane] = Result(t, result)
            if(prev_data and prev_data[lane]):
                dt = data[lane].time - prev_data[lane].time
                if(dt <= 0):
                    # HMMM how the heck did this happen
                    print("negative or zero time found....not sure what happened")
                    continue
                dv = [(v - prev_v)/dt for v, prev_v in zip(data[lane].vals, prev_data[lane].vals)]
                if(any([x < 0 for x in dv])):
                    # You'll get negative values if the JESD core was reset
                    print("negative error count found...probably a reset")
                    prev_data[lane] = data[lane]
                    continue
                else:
                    redis_db.xadd("jesd_errors", data, maxlen=500);
                    #redis_db.publish("jesd_errors", "{} {} {} {} {}".format(lane, *dv))
            prev_data[lane] = data[lane]
        sleep(2)
