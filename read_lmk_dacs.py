import hiredis
import socket

result = {}
hireader = hiredis.Reader()
conn = socket.create_connection(("127.0.0.1", 4005))

conn.sendall(b"get_available_xems\r\n")
hireader.feed(conn.recv(1024))
response = hireader.gets()
if(type(response) == hiredis.ReplyError):
    print("Error %s", str(response))
    exit()

xems = response
result = {}
for xem in xems:
    mask = (1<<xem)
    command = "set_active_xem %i\r\n" % xem
    conn.sendall(command.encode("ascii"))
    resp = conn.recv(1024)
    hireader.feed(resp)
    resp = hireader.gets()
    if(type(resp) == hiredis.ReplyError):
        print("Error %s", str(resp))
        exit()

    slot = int(xem/2)
    xem = (xem+1) % 2
    for lmk_id in [0,1,2]:
        if(xem==1 and lmk_id==2):
            continue
        command = ("read_lmk_dac_value %i\r\n" % ( lmk_id,)).encode('ascii')
        conn.sendall(command)
        resp = conn.recv(1024).decode()
        resp = int(resp[resp.find(":")+1:resp.find("\r")])
        result[(slot, xem, lmk_id)] = resp
conn.close()
for x in sorted(result.items(), key=lambda x: x[-1]):
    print(x)
