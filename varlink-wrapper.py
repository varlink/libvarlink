#!/usr/bin/env python3

import sys
import os

path_in = sys.argv[1]
path_out = sys.argv[2]
interface = os.path.basename(path_in).replace('.', '_')

with open(path_out, 'wt') as output:
    print('static const char {}[] = {{'.format(interface), file=output)
    with open(path_in) as input:
        for line in input:
            for char in line:
                print('{},'.format(ord(char)), file=output)
    print('0 };', file=output)
