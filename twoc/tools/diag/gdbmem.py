"""gdbmem.py r ADDR LEN | w ADDR HEXBYTES  -- read/write guest memory through xemu's gdb stub (tcp 1234).
The stub stops the CPU on connect; we continue ('c') before leaving."""
import socket, sys, time

def pkt(data):
    cs = sum(data.encode()) & 0xFF
    return ('$%s#%02x' % (data, cs)).encode()

def xfer(s, data, expect_reply=True):
    s.sendall(pkt(data))
    buf = b''
    t0 = time.time()
    while time.time() - t0 < 5:
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
        if not expect_reply:
            break
        i = buf.find(b'$')
        if i >= 0 and b'#' in buf[i:] and len(buf) >= buf.index(b'#', i) + 3:
            break
    s.sendall(b'+')
    if b'$' in buf:
        i = buf.index(b'$')
        return buf[i + 1:buf.index(b'#', i)].decode()
    return buf.decode(errors='replace')

s = socket.create_connection(('127.0.0.1', 1234), timeout=10)
s.sendall(b'+')
r0 = xfer(s, '?')                  # halt reason (the CPU is stopped now)
while not r0.startswith('T') and not r0.startswith('S'):
    r0 = xfer(s, '?')
def ask(cmd):
    r = xfer(s, cmd)
    n = 0
    while (r.startswith('T') or r.startswith('S') or r == '') and n < 5:
        r = xfer(s, cmd)      # a stop notification overtook the reply; ask again
        n += 1
    return r
op = sys.argv[1]
if op == 'r':
    a, n = int(sys.argv[2], 16), int(sys.argv[3])
    print(ask('m%x,%x' % (a, n)))
elif op == 'w':
    a, hx = int(sys.argv[2], 16), sys.argv[3]
    print('write', ask('M%x,%x:%s' % (a, len(hx) // 2, hx)))
    print('readback', ask('m%x,%x' % (a, len(hx) // 2)))
s.sendall(pkt('c'))                # continue
time.sleep(0.2)
s.close()
