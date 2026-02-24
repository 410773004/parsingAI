#!/usr/bin/env python3

#-----------------------------------------------------------------------------
#                 Copyright(c) 2016-2019 Innogrit Corporation
#                             All Rights reserved.
#
# The confidential and proprietary information contained in this file may
# only be used by a person authorized under and to the extent permitted
# by a subsisting licensing agreement from Innogrit Corporation.
#
#-----------------------------------------------------------------------------
import sys
import getopt
sys.path.append("./shared_tool/img_builder")
from fwconfig import *

ddr_speed = { '800': 0, '1600': 1, '2400': 2, '2666': 3, '3200': 4 }
ddr_type = { 'ddr4': 0, 'lpddr4': 1}
ddr_size = { '256' : 0, '512' : 1, '1024' : 2, '2048' : 3, '4096' : 4, '8192' : 5}

board_list = { 'm.2_0305' : 0, 'evb' : 1, 'fpga' : 2, 'u.2_0504' : 3, 'm.2_2a' : 4, 'u.2_lj' : 5}

if __name__ == '__main__':
	try:
		opts, args = getopt.getopt(sys.argv[1:], 'p:o:t:s:h:b:z:', [])
	except getopt.GetoptError:
		#usage()
		print('err')
		sys.exit(2)

	bin_file = None
	out_file = None
	size = None
	speed = None
	board = None
	dtype = None
	for opt, arg in opts:
		if opt == '-t':
			found = False
			dtype = arg
			for t in ddr_type:
				if t == dtype:
					found = True
					break

			if found == False:
				sys.exit(2)

		if opt == '-s':
			found = False
			speed = arg
			for s in ddr_speed:
				if s == speed:
					found = True
					break

			if found == False:
				sys.exit(2)
		if opt == '-b':
			bin_file = arg
		if opt == '-o':
			out_file = arg
		if opt == '-z':
			size = arg
		if opt == '-p':
			board = arg
		if opt == '-h':
			#usage()
			sys.exit(2)

	if out_file == None:
		sys.exit(2)

	fc = fwconfig()
	ret = fc.open_bin(bin_file)
	if ret == False:
		print('open fail')
		sys.exit(3)

	if speed != None:
		ret = fc.set_field('ddr_clk', ddr_speed[speed])
		if ret == False:
			print('ddr clk set fail')
			sys.exit(4)

	if dtype != None:
		ret = fc.set_field('ddr_board', ddr_type[dtype])
		if ret == False:
			print('ddr board set fail')
			sys.exit(5)

	if size != None:
		ret = fc.set_field('ddr_size', ddr_size[size])
		if ret == False:
			print('ddr size set fail')
			sys.exit(5)

	if board != None:
		ret = fc.set_field('board_id', board_list[board])
		if ret == False:
			print('board id set fail')
			sys.exit(5)

	fc.save(out_file)
