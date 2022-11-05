import socket
import hiredis

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

    xem = (xem+1) % 2
    for lmk_id in [0,1,2]:
        for pll in [0,1]:
            # XEM0 talks to LMK2, XEM1 doesn't
            if(xem==1 and lmk_id==2):
                continue
            command = ("clear_lmk_pll_status %i %i\r\n" % ( lmk_id, pll)).encode('ascii')
            conn.sendall(command)
            resp = conn.recv(1024).decode()

conn.close()
