import socket

ceres_ports = [(2, 0,4010), (2, 1, 4011), (3, 0, 4012), (3, 1, 4013),
                (4, 0, 4014), (4, 1, 4015), (5, 0, 4016), (5, 1, 4017)]
result = {}
for slot, xem, port in ceres_ports:
    try:
        conn = socket.create_connection(("localhost", port))
        for lmk_id in [0,1,2]:
            if(xem==1 and lmk_id==2):
                continue
            command = ("read_lmk_dac_value %i\r\n" % ( lmk_id,)).encode('ascii')
            conn.sendall(command)
            resp = conn.recv(1024).decode()
            resp = int(resp[resp.find(":")+1:resp.find("\r")])
            result[(slot, xem, lmk_id)] = resp

        conn.close()
    except ConnectionRefusedError:
        print("Error connecting to %i %i %i" % (slot, xem, port))

for x in sorted(result.items(), key=lambda x: x[-1]):
    print(x)
