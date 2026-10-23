#!/usr/bin/env python3
"""Hot reload of coop.dll in running games keeps a working session: start a tribe pair, reload the
mod in both games (as tools/lab.py reload does), join again, play a little, no faults."""
import time

import lane
from twoinst import Checks, start, wait_guest_in


def main():
    host, guest = start('tribe')
    ck = Checks()
    for c, who in ((host, 'host'), (guest, 'guest')):
        out = c.call('mod.reload coop')['out'].strip()
        print(f'  {who}: {out[:120]}')
        c.call('sym.reload')
    time.sleep(2)
    ck('offline' in host.call('coop.status')['out'], 'the reloaded mod starts offline')
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 120), 'host and guest play together again after the reload')
    print(' ', host.call('coop.tribe')['out'].splitlines()[0][:120])
    ck('player' in host.call('coop.tribe')['out'], 'the tribe module works after the reload')
    for c, who in ((host, 'host'), (guest, 'guest')):  # a second reload while connected
        c.call('mod.reload coop')
    time.sleep(2)
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 120), 'and after a second reload in a session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
