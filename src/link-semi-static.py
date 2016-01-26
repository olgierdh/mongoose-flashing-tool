#!/usr/bin/env python

import argparse
import re
import subprocess
import sys

parser = argparse.ArgumentParser(description='Static linking tool')
parser.add_argument('--append_static')
parser.add_argument('--force_dynamic')
parser.add_argument('--dynamic_linker')
parser.add_argument('link_cmd', nargs='+')
args = parser.parse_args()

force_dynamic_libs = set()
if args.force_dynamic:
    force_dynamic_libs.update(args.force_dynamic.split())

link_cmd = []
dynamic_libs = []
for arg in args.link_cmd:
    if arg in force_dynamic_libs:
        dynamic_libs.append(arg)
    else:
        link_cmd.append(arg)

if args.append_static:
    link_cmd.extend(args.append_static.split())

if dynamic_libs:
    link_cmd.append('-Wl,-Bdynamic')
    link_cmd.extend(dynamic_libs)

dynamic_linker = args.dynamic_linker
if not dynamic_linker:
    out = subprocess.check_output(['readelf', '-p', '.interp', sys.executable])
    m = re.search(r'\s(/\S+)', out)
    assert m, 'unrecognized readelf output format:\n%s' % out
    dynamic_linker = m.group(1)
link_cmd.append('-Wl,--dynamic-linker=%s' % dynamic_linker)

print '>>', ' '.join(link_cmd)
subprocess.check_call(link_cmd)
