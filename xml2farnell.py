from __future__ import print_function

import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

if len(sys.argv) != 3:
    print("Usage: {} <input file> <output file>".format(sys.argv[0]))
    sys.exit()

tree = ET.parse(sys.argv[1])
root = tree.getroot()
part_qtys = defaultdict(int)
part_refs = defaultdict(str)
part_desc = {}

for comp in root.iter('comp'):
    ref = comp.get('ref')
    val = comp.findtext('value')
    footprint = comp.findtext('footprint')
    fields = comp.find('fields')
    if not fields:
        continue
    for field in fields.findall('field'):
        if field.get('name') == "Farnell":
            farnell = field.text
            part_qtys[farnell] += 1
            part_refs[farnell] += " {}".format(ref)
            if farnell not in part_desc:
                part_desc[farnell] = (val, footprint)
            if part_desc[farnell] != (val, footprint):
                part_refs[farnell] = "ERROR Different parts specify same "\
                                     "order number."

with open(sys.argv[2], 'w') as f:
    for part in part_qtys:
        f.write("{}, {}, {} {}{}\n".format(part, part_qtys[part],
                                            part_desc[part][0],
                                            part_desc[part][1],
                                            part_refs[part]))