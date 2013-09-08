from collections import Counter
import gdb
import re

for x in range(4):
    print("-----------------")


l = file("gdb.txt", "rt").readlines()
l = map(lambda x: x[42:], l)
print l[0]

cnt = Counter(l)
for x in cnt.most_common(20):
    ll = x[0]
    c = x[1]

    print "%d Times" % c
    stack = re.findall("(0x................)", ll)
    for pc in stack:
        try:
            infosym = gdb.execute('info symbol %s' % pc, False, True)
            func = infosym[:infosym.find(" + ")] 
            print func  
            #print gdb.block_for_pc(eval(pc)).function
        except:
            pass
        #print pc
    
    print "\n-----"
    

