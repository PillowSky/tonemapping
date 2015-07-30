#!/bin/python

# Usage: ./main <exr image> <map image> <simple image> <fusion image>

import subprocess
import threading

task = [i for i in range(1, 19 + 1)]

class Worker(threading.Thread):
	def run(self):
		try:
			while True:
				i = task.pop()
				print("Process:", i)

				exrFile = "ori/render_result_nm_ori_%s.exr" % i
				mapFile = "map/map_%s.png" % i
				simpleFile = "simple/simple_%s.png" % i
				fusionFile = "fusion/fusion_%s.png" % i

				subprocess.call("./main %s %s %s %s" % (exrFile, mapFile, simpleFile, fusionFile), shell=True)

		except IndexError as e:
			pass

worker = []

for i in range(4):
	w = Worker()
	w.start()
	worker.append(w)

for w in worker:
	w.join()
