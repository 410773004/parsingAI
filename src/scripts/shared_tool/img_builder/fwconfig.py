#!/usr/bin/env python3

import os
import sys
import struct
import xml.dom
from xml.dom import minidom
import xml.etree.cElementTree as ET
import argparse

VER_MAJ = '1'
VER_MIN = '2'

IMAGE_CONFIG = '0x464E4F43'
PROJECT = 'INNOGRIT'

NVME_VER = '1'
BOARD_VER = '1'
FTL_VER = '3'
NCL_VER = '1'

# field, type, size, default value, value enum
project_list = ['str', 'rainier', 'shastaP']
sec_header = [['signature', 'uint', '4', IMAGE_CONFIG, []],
	['entry_point', 'uint', '4', '', []],
	['section_num', 'short', '2', '', []],
	['image_dus', 'short', '2', '1', []],
	['section_csum', 'uint', '4', '', []],
	['project', 'string', '8', PROJECT, project_list],
	['version_major', 'uint', '4', VER_MAJ, []],
	['version_minor', 'uint', '4', VER_MIN, []],
	['header_csum', 'uint', '4', '', []]]
rainier_board_id = ['m.2_0305', 'evb', 'fpga', 'u.2_0504', 'm.2_2a', 'u.2_lj']
shasta_plus_bord_id = []
sec_board = [['version', 'uint', '4', BOARD_VER, []],
	['board_id', 'byte', '1', '0', ['list', project_list, rainier_board_id, shasta_plus_bord_id]],
	['odt', 'byte', '1', '0', []],
	['cpu_clk', 'byte', '1', '0', []],
	['ddr_board', 'byte', '1', '0', ['idx', 'ddr4','lpddr4']],
	['ddr_clk', 'byte', '1', '1', ['idx', '800', '1600','2400','2666','3200']],
	['ddr_size', 'byte', '1', '0', ['idx', '256', '512', '1024', '2048', '4096', '8192']],
	['ddr_vendor', 'byte', '1', '0', ['idx', 'default', 'micron', 'hynix', 'samsung', 'nanya']],
	['rsvd1', 'byte', '117', '', []],
	['ddr_info', 'byte', '320','', []]]
sec_nvme = [['version', 'uint', '4', NVME_VER, []],
	['pmu_support', 'byte', '1', '0', []]]
sec_ftl = [['version', 'uint', '4', FTL_VER, []],
	['op', 'uint', '4', '', []],
	['tbl_op', 'uint', '4', '', []],
	['tbw', 'short', '2', '', []],
	['burst_wr_mb', 'short', '2', '', []],
	['slc_ep', 'short', '2', '', []],
	['native_ep', 'short', '2', '', []],
	['user_spare', 'short', '2', '', []],
	['read_only_spare', 'byte', '1', '', []],
	['wa', 'byte', '1', '', []],
	['nat_rd_ret_thr', 'uint', '4', '', []],
	['slc_rd_ret_thr', 'uint', '4', '', []],
	['max_wio_size', 'uint', '4', '', []],
	['gc_retention_chk', 'byte', '1', '', []],
	['alloc_retention_chk', 'byte', '1', '', []],
	['avail_spare_thr', 'byte', '1', '', []]]
sec_ncl = [['version', 'uint', '4', NCL_VER, []],
	['eccu_clk', 'byte', '1', '0', []],
	['nand_clk', 'byte', '1', '0', []],
	['nand_dll_ch', 'byte', '16', '0', []],
	['nand_ds_ch', 'byte', '16', '0', []]]

config = [[sec_header, 'header', 'struct', '512'],
	[sec_board, 'board', 'struct', '512'],
	[sec_nvme, 'nvme', 'struct', '512'],
	[sec_ftl, 'ftl', 'struct', '512'],
	[sec_ncl, 'ncl', 'struct', '2048']]

#xml_file = 'fwconfig.xml'
#bin_file = 'fwconfig.bin'
header_file = 'fwconfig.h'

ig_copyright = ('//-----------------------------------------------------------------------------\n',
		'//		 Copyright(c) 2016-2019 Innogrit Corporation\n',
		'//			     All Rights reserved.\n',
		'//\n',
		'// The confidential and proprietary information contained in this file may\n',
		'// only be used by a person authorized under and to the extent permitted\n',
		'// by a subsisting licensing agreement from Innogrit Corporation.\n',
		'// Dissemination of this information or reproduction of this material\n',
		'// is strictly forbidden unless prior written permission is obtained\n',
		'// from Innogrit Corporation.\n',
		'//-----------------------------------------------------------------------------\n',
		'\n',
		)

class fwconfig(object):
	def __init__(self):
		self.xml_root = None

	def set_field(self, field_name, value):
		if self.xml_root == None:
			return False

		for field in self.xml_root.iter(field_name):
			if type(value) is int:
				field.text = str(value)
			else:
				field.text = value

		return True

	def save(self, outfile, binary=True):
		if binary == False:
			# save XML
			xmlstr = xml.dom.minidom.parseString(ET.tostring(self.xml_root)).toprettyxml(indent="\t")
			xmlstr = xmlstr[xmlstr.find('\n')+1:]   # remove version line
			ET.ElementTree(ET.fromstring(xmlstr)).write(outfile, short_empty_elements=False)
			return

		# save binary
		fp = open(outfile, "wb")

		config = self.xml_root
		ttl_size = int(config.get('size'))

		for sec in config:
			sec_size = int(sec.get('size'))
			ttl_size -= sec_size
			for field in sec:
				field_size = int(field.get('size'))
				field_type = field.get('type')
				value = field.text
				if value is None:
					value = '0'
				buff = bytearray(field_size)
				if field_type == 'string':
					value = value.ljust(field_size, '\0')
					buff = bytes(value, "utf8")
				else:
					if value[0:2] == '0x':
						buff = int(value, 16).to_bytes(field_size, byteorder="little")
					else:
						buff = int(value).to_bytes(field_size, byteorder="little")
				fp.write(buff)
				sec_size -= field_size

			# fill the rest field in section
			buff = bytearray(sec_size)
			fp.write(buff)

		# (not used) fill the rest field in config
		#buff = bytearray(ttl_size)
		#fp.write(buff)
		fp.close()

	def open_xml(self, xml_file):
		tree = ET.parse(xml_file)
		self.xml_root = tree.getroot()

	def open_bin(self, bin_file):
		fp = open(bin_file, "rb")

		self.xml_root = ET.Element('config', type="struct", size="4096")

		for sec in config:
			sec_name = sec[1]
			sec_type = sec[2]
			sec_size = int(sec[3])
			sec_name = ET.SubElement(self.xml_root, sec_name, type=sec_type, size=str(sec_size))

			for field in sec[0]:
				field_name = field[0]
				field_type = field[1]
				field_size = int(field[2])
				if field_type == "string":
					field_value = str(fp.read(field_size), encoding='utf-8').strip('\x00')
				else:
					field_value = int.from_bytes(fp.read(field_size), byteorder='little')
					if field_name == "signature":
						field_value = hex(field_value).upper()
						if field_value[0:2] == '0X':
							field_value1 = field_value[2:]
							field_value = field_value[0:2].lower() + field_value1

				ET.SubElement(sec_name, field_name, type=field_type, size=str(field_size)).text = str(field_value)

				sec_size -= field_size

			# skip reserved field
			fp.read(sec_size)

		fp.close()

	def xml_to_bin(self, xml_file):
		bin_file = xml_file + '.bin'
		xml_file = xml_file + '.xml'

		try:
			os.remove(bin_file)
		#except OSError as e:
		#	print(e)
		except:
			pass

		self.open_xml(xml_file)
		self.save(bin_file)

	def bin_to_xml(self, bin_file):
		xml_file = bin_file + '.xml'
		bin_file = bin_file + '.bin'

		try:
			os.remove(xml_file)
		#except OSError as e:
		#	print(e)
		except:
			pass

		self.open_bin(bin_file)

		xmlstr = xml.dom.minidom.parseString(ET.tostring(self.xml_root)).toprettyxml(indent="\t")
		xmlstr = xmlstr[xmlstr.find('\n')+1:]   # remove version line
		ET.ElementTree(ET.fromstring(xmlstr)).write(xml_file, short_empty_elements=False)

	def create_xml_template(self, xml_file):
		try:
			os.remove(xml_file)
		#except OSError as e:
		#	print(e)
		except:
			pass

		xml_root = ET.Element('config', type="struct", size="4096")
		for sec in config:
			sec_name = sec[1]
			sec_type = sec[2]
			sec_size = int(sec[3])
			sec_name = ET.SubElement(xml_root, sec_name, type=sec_type, size=str(sec_size))

			for field in sec[0]:
				field_name = field[0]
				field_type = field[1]
				field_size = int(field[2])
				field_value = field[3]

				ET.SubElement(sec_name, field_name, type=field_type, size=str(field_size)).text = field_value

		xmlstr = xml.dom.minidom.parseString(ET.tostring(xml_root)).toprettyxml(indent="\t")
		xmlstr = xmlstr[xmlstr.find('\n')+1:]   # remove version line

		ET.ElementTree(ET.fromstring(xmlstr)).write(xml_file, short_empty_elements=False)

	def generate_c_header(self):
		try:
			os.remove(header_file)
		#except OSError as e:
		#	print(e)
		except:
			pass

		fp = open(header_file, 'w')

		# copyright
		for line in ig_copyright:
			fp.write(line)

		fp.write('#pragma once\n\n')
		fp.write('#define IMAGE_CONFIG ' + IMAGE_CONFIG + '\t/* CONF */\n')
		fp.write('#define FWCFG_LENGTH 4096\n')
		fp.write('#define VERSION_MAJ ' + VER_MAJ + '\n')
		fp.write('#define VERSION_MIN ' + VER_MIN + '\n')
		fp.write('#define BOARD_CFG_VER ' + BOARD_VER + '\n')
		fp.write('#define NVME_CFG_VER ' + NVME_VER + '\n')
		fp.write('#define FTL_CFG_VER ' + FTL_VER + '\n')
		fp.write('#define NCL_CFG_VER ' + NCL_VER + '\n')
		fp.write('\n')

		for sec in config:
			sec_name = sec[1]
			sec_type = sec[2]
			sec_size = int(sec[3])

			fp.write('typedef struct {\n')
			for field in sec[0]:
				fp.write('\t')
				field_name = field[0]
				field_type = field[1]
				field_size = int(field[2])

				sec_size -= field_size

				if field_type == 'uint':
					fp.write('unsigned int ')
					field_size >>= 2
				elif field_type == 'short':
					fp.write('unsigned short ')
					field_size >>= 1
				elif field_type == 'byte' or field_type == 'string':
					fp.write('char ')

				if field_size > 1:      # array
					fp.write(field_name + '[' + str(field_size) + '];\n')
				else:
					fp.write(field_name + ';\n')

			if sec_size > 0:
				fp.write('\tchar reserved[' + str(sec_size) + '];\n')

			fp.write('} ' + sec_name + '_cfg_t;\n\n')

		fp.write('typedef struct _fw_config_set_t {\n')
		for sec in config:
			sec_name = sec[1]
			fp.write('\t' + sec_name + '_cfg_t ' + sec_name + ';\n')
		fp.write('} fw_config_set_t;\n')

		fp.close()


def process_command():
	parser = argparse.ArgumentParser()
	parser.add_argument('--temp', '-t', type=str, required=False, help='create template')
	parser.add_argument('--header', '-g', action='store_true', required=False, help='create c header')
	parser.add_argument('--xml2bin', '-x', type=str, required=False, help='xml to bin, input file without .xml')
	parser.add_argument('--bin2xml', '-b', type=str, required=False, help='bin to xml, input file without .bin')
	return parser.parse_args()

if __name__ == '__main__':
	args = process_command()
	fc = fwconfig()

	if args.temp != None:
		fc.create_xml_template(args.temp)
	elif args.header:
		fc.generate_c_header()
	elif args.xml2bin != None:
		fc.xml_to_bin(args.xml2bin)
	elif args.bin2xml:
		fc.bin_to_xml(args.bin2xml)

