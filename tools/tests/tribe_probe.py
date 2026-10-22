#!/usr/bin/env python3
"""Start host+guest in the tribe fixture and print the tribe state on both (manual RE aid)."""
from twoinst import start

host, guest = start('tribe', 'Game0/Cohrach')
for name, con in (('host', host), ('guest', guest)):
    print('==', name, con.call('coop.avatar')['out'].splitlines()[:1])
    print(con.call('coop.tribe')['out'])
