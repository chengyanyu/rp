import sys
import numpy as np


def main(argv=sys.argv):
	if len(argv) != 3:
		print "\n  Usage: %s input output"%argv[0]
		print "\n  input: generated by `gap_predict`"
		print "  output: each line will be `name std sumx2`"
		print ""
		sys.exit(1)

	inf = open(argv[1],'r')
	outf = open(argv[2],'w')
	print >>outf, "name std std/mean sumx2"
	for line in inf:
		line = line.split()
		if len(line) == 2:
			print line[0],"failed to be predicted"
			continue
		if ':' in line[2]:
			j = 2
		else:
			j = 3
		predict = float(line[j-1])
		vals = np.asarray([pow(10, float(item.split(":")[1])-predict) for item in line[j:]])
		mean = np.mean(vals)
		std  = np.std(vals,ddof=1)
		sumx2= np.sum(vals**2)
		print >>outf, line[0],std,std/mean,sumx2
	outf.close()

main()
