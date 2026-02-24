#!/usr/bin/env python

#-----------------------------------------------------------------------------
#                 Copyright(c) 2016-2019 Innogrit Corporation
#                             All Rights reserved.
#
# The confidential and proprietary information contained in this file may
# only be used by a person authorized under and to the extent permitted
# by a subsisting licensing agreement from Innogrit Corporation.
#
#-----------------------------------------------------------------------------

import os
import re
import sys
import struct
import string
import subprocess
import re
import unicodedata
import binascii
import getopt
import shutil
import platform
import logging as log
from ctypes import *

SEC_NAME = 0
SEC_LMA = 2
SEC_SIZE = 4
READELF = 'arm-none-eabi-readelf -S %s'
DUMP_SECTION = 'arm-none-eabi-objcopy -O binary -j %s %s ./section/%s'
PARSE_EP = 'arm-none-eabi-readelf -h %s'
PARSE_FWC = 'arm-none-eabi-nm %s | grep __dtag_fwconfig_start'
IMAGE_SIGNATURE = '0x54495247'
IMAGE_COMBO = '0x424D4F43'
IMAGE_COMBO_WITH_FWCONFIG = '0x47464D43'
INVALID_PMA = '0xffffffff'
MAX_FW_SLICE = 8
VERBOSE = False

class fw_slice_t(Structure):
	_fields_ = [("slice_start", c_uint32), ("slice_end", c_uint32)]

class fw_slice_array(Array):
	_type_ = fw_slice_t
	_length_ = MAX_FW_SLICE

class uint32_array(Array):
	_type_ = c_uint32
	_length_ = 20

class section_t(Structure):
	_fields_ = [("identifier", c_uint32), ("offset", c_uint32), ("length", c_uint32), ("pma", c_uint32)]

	def pack(self):
		return struct.pack('4I', self.identifier, self.offset, self.length, self.pma)

class section_array(Array):
	_type_ = section_t
	_length_ = 80

class u_type_t(Union):
	_fields_ = [("section", section_array), ("fw_slice", fw_slice_array)]

class image_t(Structure):
	_fields_ = [("signature", c_uint32), ("entry_point", c_uint32),
				("section_num", c_uint16), ("image_dus", c_uint16), ("section_csum", c_uint32), ("u_type", u_type_t)]
	def pack(self):
		return struct.pack('IIHHI', self.signature, self.entry_point, self.section_num, self.image_dus, self.section_csum)

class bin_tag_t():
    def __init__(self, fw_version):
        # input whole fw_version eg. EN248V90
        self.bin_tag = "SsdBinTagNVMe"
        self.subversion = fw_version[-1]
        self.model_name = "SSSTC LJ1-2W$"[0:40]
        self.fw_revision = fw_version[0:8][0:-1]
        self.oem_fw_revision = fw_version[0:8][0:-1]
        # use the date right now
        self.component_id = ""
        self.oem_name = ""
        self.support_capacity = [3840, 7680, 0, 0, 0, 0]
    def str2hex(self, str1):
		# reverse the input string and convert every char to integer in hex
        str1 = str1[::-1]
        result = "0x"
        for c in str1:
            if c == '\0':
                result += '00'
            else:
                result += hex(ord(c)).lstrip("0x")
        return result
    def format(self):
        self.bin_tag = self.bin_tag.ljust(15, "\0")
        self.model_name = self.model_name.ljust(40, "\0")
        self.fw_revision = self.fw_revision.ljust(8, " ")
        self.oem_fw_revision = self.oem_fw_revision.ljust(8, " ")
        self.component_id = self.component_id.ljust(8, " ")
        self.oem_name = self.oem_name.ljust(4, " ")
        self.field = [int(self.str2hex(self.bin_tag[0:8]),16), int(self.str2hex(self.bin_tag[8:15]+self.subversion), 16),
        int(self.str2hex(self.model_name[0:8]), 16), int(self.str2hex(self.model_name[8:16]), 16), int(self.str2hex(self.model_name[16:24]), 16),
        int(self.str2hex(self.model_name[24:32]), 16), int(self.str2hex(self.model_name[32:40]), 16), int(self.str2hex(self.fw_revision), 16), int(self.str2hex(self.oem_fw_revision), 16),
        int(self.str2hex(self.component_id), 16), int(self.str2hex(self.oem_name), 16), int(self.support_capacity[0]), int(self.support_capacity[1]), int(self.support_capacity[2]),
        int(self.support_capacity[3]), int(self.support_capacity[4]), int(self.support_capacity[5])]

    def pack(self):
        return struct.pack("QQQQQQQQQQIHHHHHH", self.field[0], self.field[1], self.field[2], self.field[3], self.field[4], self.field[5],self.field[6], self.field[7],
        self.field[8], self.field[9], self.field[10], self.field[11], self.field[12], self.field[13], self.field[14], self.field[15], self.field[16])

def usage():
	print("-a: enable section offset align to 32 bytes in image")
	print("-b: enable combine mode, to combine fw/config/loader")
	print("-c [CONFIG FILE NAME]: config file name")
	print("-d [CPU_ID]: indicate cpu index of image 0 base")
	print("-f [FIRMWRE FILE NAME]: firmware image name")
	print("-h: show help message")
	print("-i [ELF NAME]: indicate elf")
	print("-k [SECURITY KEY]: private key to sign image")
	print("-l [LOADER FILE NAME]: loader firmware image name")
	print("-p: indicate SHASTA-PLUS/RAINIER family")
	print("-o [OUTPUT FILE NAME]: output file name")
	print("-v: enable verbose")
	print("-z: exclude zi/ni section in image")
	print("usage: " + sys.argv[0] + " -p -i elf_1 -d 0 -i elf_2 -d 1 -o firmware.fw")
	print("usage: " + sys.argv[0] + " -p -b -l loader.fw -f firmware.fw [-c fwconfig.bin] -o shasta_combo.fw")

def align(num, alignment):
	return int(((num + alignment - 1) / alignment)) * alignment

def read_section(elf):
	section_list = list()
	output = subprocess.check_output(READELF % elf, shell=True)
	if sys.version_info[0] >= 3:
		output = bytes.decode(output)

	output = output.splitlines(True)
	for a in output:
		a = a.strip('\r\n')
		if 'PROGBITS' in a and re.search('[W|AX]', a):
			a = re.sub('\[ *\d*\]', '', a)	# remove [ 1] or [10]
			section_list.append(a)

	return section_list

def file_pad(fid, sz, crc):
	pad = bytearray(sz)
	crc = binascii.crc32(pad, crc) & 0xffffffff
	fid.write(pad)
	return crc

def add_cpu_config(num, crc, fwc=0):
	bitmap = (1 << num) - 1
	s = struct.pack('2I', bitmap, fwc)
	crc = binascii.crc32(s, crc) & 0xffffffff
	f.write(s)
	crc = file_pad(f, 8, crc)
	return crc

def set_4K_aligned(sz):
        if sz & 0xFFF:
                sz += 0x1000 - (sz & 0xFFF)
        return sz

def check_4K_aligned(f):
	if os.path.getsize(f) & 0xFFF:
		return False
	return True

def get_entry_point(elf):
	output = subprocess.check_output(PARSE_EP % elf, shell=True)
	if sys.version_info[0] >= 3:
		output = bytes.decode(output)

	output = output.splitlines(True)
	for line in output:
		if 'Entry point address' in line:
			line = line.rsplit()
			return line[3]

	return None

def get_fwc_location(elf):
	try:
		output = subprocess.check_output(PARSE_FWC % elf, shell=True)
	except:
		print("no fwc position")
		return 0

	if sys.version_info[0] >= 3:
		output = bytes.decode(output)
	output = output.split(' ')
	return int(output[0], 16)


def get_section_id(buf):
	if len(buf[1]) >= 4:
		return c_uint(ord(buf[1][len(buf[1])-1]) << 24 | ord(buf[1][len(buf[1])-2]) << 16 | ord(buf[1][1]) << 8 | ord(buf[1][0]))
	elif len(buf[1]) == 3:
		return c_uint(ord(buf[1][2]) << 16 | ord(buf[1][1]) << 8 | ord(buf[1][0]))
	elif len(buf[1]) == 2:
		return c_uint(ord(buf[1][1]) << 8 | ord(buf[1][0]))
	elif len(buf[1]) == 1:
		return c_uint(ord(buf[1][0]))

def get_revision(elf):
	ret = ''
	with open(elf, "rb") as f:
		result = f.read()
	result = result.partition(b'top, ')[2][0:20]
	pattern = re.compile('[a-zA-Z0-9]*\.[a-zA-Z0-9]*\.[a-zA-Z0-9]*\.[a-zA-Z0-9]*')
	for match in re.finditer(pattern, result.decode('utf-8')):
		ret = match.group()
		ret = ret.split('.')
		for i in range(4):
			if ret[i][0] == 'r':
				ret[i] = ret[i][1]
			if len(ret[i]) != 2:
				ret[i] = ret[i].rjust(2, '\0')
		return str(''.join(ret))

	return ret.ljust(8, '\0')

def get_nand_type(elf):
	ret = ''
	with open(elf, "rb") as f:
		result = f.read()
		result = result.partition(b'du(')[2][0:10]
	pattern = re.compile('(tsb|unic|mu|sndk|ss|hynx)')
	for match in re.finditer(pattern, result.decode('utf-8')):
		ret = match.group()
		ret = ret.ljust(8, '\0')
		return str(''.join(ret))

	return ret.ljust(8, '\0')

def prepare_tmp_key_dir(key):
        shutil.rmtree('./keys_temp', ignore_errors=True)
        os.mkdir('./keys_temp')
        shutil.copy2(key, './keys_temp/key.pem')

def prepare_secure_section(idx, identifier, length, offset):
        image.u_type.section[idx].identifier = c_uint(ord(identifier[3]) << 24 | ord(identifier[2]) << 16 | ord(identifier[1]) << 8 | ord(identifier[0]))
        image.u_type.section[idx].length = c_uint(length)
        image.u_type.section[idx].pma = c_uint(int(INVALID_PMA, 16))
        image.u_type.section[idx].offset = c_uint(offset)

def shax_256_str2hex(sha3_256_file):
	with open(sha3_256_file, 'rb') as f:
		s = f.read()
	return binascii.unhexlify(s.decode())

def add_key_section(key_file, section_idx, section_slice):
	import pp_key
	result = pp_key.extract_pubkey('%s' % key_file)
	f = open('keys_temp/pub_key.bin', 'wb')
	f.write(result)
	f.close()
	filesize = os.path.getsize('keys_temp/pub_key.bin')
	prepare_secure_section(section_idx, 'PKEY', filesize, section_slice)

def gen_hash(method, input_file, output_file):
	if method == "sha3_256":
		s = hashlib.sha3_256()
	elif method == "sha256":
		s = hashlib.sha256()

	with open("%s" % input_file, "rb") as f, open("%s" % output_file, "wb") as f_w:
		buf = f.read()
		s.update(buf)
		result = s.hexdigest();
		f_w.write(result.encode())
	f_w.close()

def add_key_sha3_256_section(key_file, section_idx, section_slice):
	gen_hash("sha3_256", "%s" % key_file, "keys_temp/key.sha3-256");
	prepare_secure_section(section_idx, 'KOTP', 32, section_slice)

def add_fw_slice_info(idx, sz):
	if idx >= MAX_FW_SLICE:
		print("Max fw slice is 8!")
		sys.exit(2)
	if sz == 0:
		image.u_type.fw_slice[idx].slice_start = image.u_type.fw_slice[idx].slice_end = 0
	else:
		image.u_type.fw_slice[idx].slice_start = 1 if idx == 0 else image.u_type.fw_slice[idx-1].slice_end + 1
		image.u_type.fw_slice[idx].slice_end = int(image.u_type.fw_slice[idx].slice_start + ((sz + 0x1000 - 1 ) / 0x1000) - 1)

def check_if_ni_zi(identifier):
	if (identifier & 0xFFFF0000) == 0x696e0000:
		return True
	elif (identifier & 0xFFFF0000) == 0x697a0000:
		return True
	else:
		return False

def remove_zi_ni_id(identifier):
	return (identifier & 0xFF00FFFF) | 0x006c0000; # change ni/zi to li

def check_section(section_name, target):
	if target in section_name:
		return True
	return False

if __name__ == '__main__':
	#input para check

	cpu_idx = 0
	combine = False
	sec_img = False
	security_revision = False
	ld = ""
	cfg = ""
	key = ""
	cpus = []
	fws = []
	elfs = []
	cpu_flag = False
	output = ""
	revision = ""
	nand_info = ""
	DU_SZE = 2048
	HEADER_SZ = CPU_CONFIG_SZ = PADDING_SZ = 16
	REVISON_SZ = NAND_INFO_SZ = 8
	ATCM_cpu_base_offset = [0x0, 0x180000, 0x200000, 0x280000]
	BTCM_cpu_base_offset = [0x80000, 0x1a0000, 0x220000, 0x2a0000]
	wins = False
	shastap = False
	section_align = False
	section_align_pad = []
	include_zi_ni_bin = True
	fwc = 0

	if platform.system() == "Windows":
		wins = True

	if sys.version_info < (3, 6):
		import sha3

	try:
		opts, args = getopt.getopt(sys.argv[1:], 'h:i:o:l:f:c:k:d:r:abpvzs', [])
	except getopt.GetoptError:
		usage()
		sys.exit(2)

	for opt, arg in opts:
		if opt == "-v":
			VERBOSE = True
		if opt == "-z":
			include_zi_ni_bin = False
		if opt == "-a":
			section_align = True
		if opt == "-i":
			elfs.append(arg)
		if opt == "-d":
			cpus.append(arg)
		if opt == "-o":
			output = arg
		if opt == "-c":
			cfg = arg
		if opt == "-b":
			combine = True
		if opt == "-l":
			ld = arg
		if opt == "-p":
			shastap = True
		if opt == "-f":
			fws.append(arg)
		if opt == "-k":
			sec_img = True
			key = arg
		if opt == "-h":
			usage()
			sys.exit(2)
		if opt == "-r":
			cust_fr_ver = arg
		if opt == "-s":
		    security_revision = True

	if shastap:
		DU_SZE = 4096
	DU_MSK = DU_SZE - 1

	if VERBOSE == True:
		log.basicConfig(format="%(levelname)s: %(message)s", level=log.DEBUG)
		log.info("Verbose output.")
	else:
		log.basicConfig(format="%(levelname)s: %(message)s")

	if section_align == True:
		log.info("section align enabled")

	if include_zi_ni_bin == False:
		log.info("exclude zi ni section")

	if combine == True:
		if ld == "" or fws == [] or output == "":
			usage()
			sys.exit(2)
		if check_4K_aligned(fws[0]) == False:
			log.error("Invalid size (%d) of fw: %s, it should be 4K aligned!"
					% (os.path.getsize(fws[0]), fws[0]))
			sys.exit(2)
		if cfg != "" and check_4K_aligned(cfg) == False:
			log.error("Invalid size (%d) of cfg: %s, it should be 4K aligned!"
					% (os.path.getsize(cfg), cfg))
			sys.exit(2)
	else:
		if len(elfs) == 0 or output == "":
			usage()
			sys.exit(2)

	image = image_t()

	if combine:
		slice_idx = 0
		if cfg != "":
			image.signature = c_uint(int(IMAGE_COMBO_WITH_FWCONFIG, 16))
		else:
			image.signature = c_uint(int(IMAGE_COMBO, 16))
		section_slice = 4096

		ld_sz = os.path.getsize(ld)

		add_fw_slice_info(slice_idx, ld_sz)
		slice_idx += 1
		section_slice += ld_sz
		section_slice = set_4K_aligned(section_slice)

		fw_sz = os.path.getsize(fws[0])
		add_fw_slice_info(slice_idx, fw_sz)
		slice_idx += 1
		section_slice += fw_sz
		section_slice = set_4K_aligned(section_slice)

		if cfg != "":
			fwconfig_sz = os.path.getsize(cfg)
			add_fw_slice_info(slice_idx, fwconfig_sz)
			slice_idx += 1
			section_slice += fwconfig_sz
			section_slice = set_4K_aligned(section_slice)

		''' Add other unused fw slice info here '''

		for i in range(slice_idx, MAX_FW_SLICE):
			add_fw_slice_info(i, 0)
			section_slice += 0
			section_slice = set_4K_aligned(section_slice)

		'''
		for i in range(mfw.slices):
			print (mfw.fw_slice[i].slice_start, mfw.fw_slice[i].slice_end)

		print (image.u_type.fw_slice[0].slice_start, image.u_type.fw_slice[0].slice_end)
		print (image.u_type.fw_slice[1].slice_start, image.u_type.fw_slice[1].slice_end)
		'''
		# ============= end of condition of combine == 1 ===============
	else:
		image.signature = c_uint(int(IMAGE_SIGNATURE, 16))
		ep = get_entry_point(elfs[0])
		fwc = get_fwc_location(elfs[0])
		log.info("FWCONFIG location: %s" % hex(fwc))
		image.entry_point = c_uint(int(ep, 16))

		shutil.rmtree('./section', ignore_errors=True)
		os.mkdir('./section')

		for i in range(len(elfs)):
			section_list = read_section(elfs[i])	# get section list
			for s in section_list:
				s = s.split()
				if wins:
					_S = "section%d%s$%s$%s" %(i, s[SEC_NAME], s[SEC_LMA], s[SEC_SIZE])
				else:
					_S = "section%d%s\$%s\$%s" %(i, s[SEC_NAME], s[SEC_LMA], s[SEC_SIZE])
				os.system(DUMP_SECTION % (s[SEC_NAME], elfs[i], _S))

		revision = get_revision(elfs[0])
		nand_info = get_nand_type(elfs[0])
		section_name = os.listdir("section")
		section_name.sort()
		image.section_num = c_ushort(int(len(section_name)))
		secure_section_num = 0
		section_idx = 0
		for i in section_name:
			buf = i
			buf = buf.replace('$', '\0', 1)
			buf = buf.replace('.', '\0', 1)
			buf = buf.split('\0')
			elf_idx = int(buf[0][7])

			image.u_type.section[section_idx].identifier = get_section_id(buf)
			pma = buf[2].replace('$', '\0', 1).split('\0')[0]
			length = buf[2].replace('$', '\0', 1).split('\0')[1]
			image.u_type.section[section_idx].pma = c_uint(int(pma, 16))
			image.u_type.section[section_idx].length = c_uint(int(length, 16))

			# Identifier is ATCM or BTCM
			if check_section(buf[1], 'ATCM'):
				msg = "mem2dma: " + hex(image.u_type.section[section_idx].pma) + "->"
				image.u_type.section[section_idx].pma += ATCM_cpu_base_offset[elf_idx]
				msg = msg + hex(image.u_type.section[section_idx].pma)
				log.info(msg)
			elif check_section(buf[1], 'BTCM'):
				msg = "mem2dma: " + hex(image.u_type.section[section_idx].pma) + "->"
				image.u_type.section[section_idx].pma += BTCM_cpu_base_offset[elf_idx] - 0x80000
				msg = msg + hex(image.u_type.section[section_idx].pma)
				log.info(msg)

			section_idx += 1

		section_slice = HEADER_SZ + 16 * image.section_num
		section_slice += (16 * 5) if sec_img else 0 # reserve 5 section for secure boot

		for i in range(image.section_num):
			if section_align == True:
				section_align_pad.append(align(section_slice, 32) - section_slice)
				section_slice += section_align_pad[i]
			else:
				section_align_pad.append(0)

			image.u_type.section[i].offset = c_uint(int(section_slice))
			if include_zi_ni_bin == True or check_if_ni_zi(image.u_type.section[i].identifier) == False:
				section_slice = section_slice + image.u_type.section[i].length
				msg = "include: [%d]" % i
				if check_if_ni_zi(image.u_type.section[i].identifier) == True :
					msg = msg + " " + hex(image.u_type.section[i].identifier)
					image.u_type.section[i].identifier = remove_zi_ni_id(image.u_type.section[i].identifier)
					msg = msg + "->" + hex(image.u_type.section[i].identifier) + " = "
			else:
				msg = "exclude:"

			msg = msg + hex(image.u_type.section[i].identifier) + "->" + str(image.u_type.section[i].length)
			msg = msg + " offset:" + str(image.u_type.section[i].offset)
			log.info(msg)
		if sec_img:
			if sys.version_info[0] >= 3 and sys.version_info[1] >= 4:
				import importlib
			else:
				import imp
			import hashlib
			from Crypto.PublicKey import RSA
			from Crypto.Signature import PKCS1_v1_5
			from Crypto.Hash import SHA256

			prepare_tmp_key_dir(key)
			add_key_section('./keys_temp/key.pem', image.section_num, section_slice)
			section_slice = section_slice + image.u_type.section[image.section_num].length
			secure_section_num += 1
			image.section_num += 1

			add_key_sha3_256_section('keys_temp/pub_key.bin', image.section_num, section_slice)
			key_sha_buf = shax_256_str2hex('keys_temp/key.sha3-256')
			section_slice = section_slice + image.u_type.section[image.section_num].length
			secure_section_num += 1
			image.section_num += 1

			# add fw pkg sha3 value
			prepare_secure_section(image.section_num, 'FSHA', 32, section_slice)
			fw_sha_slice = section_slice
			section_slice = section_slice + image.u_type.section[image.section_num].length
			secure_section_num += 1
			image.section_num += 1

			# add fw pkg sha3 value's sha256 value
			prepare_secure_section(image.section_num, 'SSHA', 32, section_slice)
			sha_sha_slice = section_slice
			section_slice = section_slice + image.u_type.section[image.section_num].length
			secure_section_num += 1
			image.section_num += 1

			# add fw pkg sha3 value's signature
			prepare_secure_section(image.section_num, 'SSIG', 256, section_slice)
			fw_sig_slice = section_slice
			section_slice = section_slice + image.u_type.section[image.section_num].length
			secure_section_num += 1
			image.section_num += 1

			write_size = align(section_slice + (CPU_CONFIG_SZ if combine == 0 else 0), 32)
			image.image_dus = int((write_size + PADDING_SZ + REVISON_SZ + NAND_INFO_SZ + DU_MSK) / DU_SZE)
			crc = 0
			for i in range(image.section_num):
				s = image.u_type.section[i].pack()
				crc = binascii.crc32(s, crc) & 0xffffffff
			image.section_csum = c_uint(crc)

			f = open('keys_temp/temp.fw', 'wb')
			s = image.pack()
			f.write(s)

			for i in range(image.section_num):
				s = image.u_type.section[i].pack()
				f.write(s)

			for i in range(image.section_num - secure_section_num):
				with open('./section/%s' % section_name[i], 'rb') as fp_read:
					if section_align_pad[i] != 0:
						pad = bytearray(section_align_pad[i])
						f.write(pad)

					if include_zi_ni_bin == True or check_if_ni_zi(image.u_type.section[i].identifier) == False:
						s = fp_read.read()
						f.write(s)
			f.close()

			gen_hash("sha3_256", "keys_temp/temp.fw", "keys_temp/shasta.fw.sha3-256");
			fw_sha_slice_buf = shax_256_str2hex('keys_temp/shasta.fw.sha3-256')
			with open('keys_temp/shasta.fw.sha3-256.bin', 'wb') as f:
				f.write(fw_sha_slice_buf)
			f.close()

			gen_hash("sha256", "keys_temp/shasta.fw.sha3-256.bin", "keys_temp/shasta.sha3.sha256");
			sha_sha_slice_buf = shax_256_str2hex('keys_temp/shasta.sha3.sha256')

			f = open('keys_temp/key.pem','r')
			key = RSA.importKey(f.read())
			signer = PKCS1_v1_5.new(key)
			digest = SHA256.new()
			with open('keys_temp/shasta.fw.sha3-256.bin', 'rb') as f, open('keys_temp/shasta.fw.sha3-256.signature', 'wb') as f_w:
				digest.update(f.read())
				f_w.write(signer.sign(digest))
				fw_sig_slice_buf = signer.sign(digest)
			f_w.close()
			image.image_dus = int((section_slice + DU_MSK)/DU_SZE)
		else:
			# sec_img == 0
			image.image_dus = int((section_slice + DU_MSK)/DU_SZE)
			crc = 0
			for i in range(image.section_num):
				s = image.u_type.section[i].pack()
				crc = binascii.crc32(s, crc) & 0xffffffff
			image.section_csum = c_uint(crc)

		# ============= end of condition of combine == 0 ===============

	ori_towrite = towrite = section_slice + (CPU_CONFIG_SZ if combine == 0 else 0)

	towrite = align(towrite, 32)

	if combine:
		image.image_dus = int((towrite + PADDING_SZ + DU_MSK) / DU_SZE)
	else:
		image.image_dus = int((towrite + PADDING_SZ + REVISON_SZ + NAND_INFO_SZ + DU_MSK) / DU_SZE)

	f = open('%s' % output, 'wb')
	crc = 0
	# Header: 16 bytes
	s = image.pack()
	crc = binascii.crc32(s, crc) & 0xffffffff
	f.write(s)

	if combine:
		isz = len(s)
		for i in range(MAX_FW_SLICE):
			s = struct.pack("2I", image.u_type.fw_slice[i].slice_start, image.u_type.fw_slice[i].slice_end)
			crc = binascii.crc32(s, crc) & 0xffffffff
			f.write(s)
			isz += len(s)
		binTag = bin_tag_t(cust_fr_ver)
		binTag.format()
		s = binTag.pack()
		crc = binascii.crc32(s, crc) & 0xffffffff
		f.write(s)
		isz += len(s)

		crc = file_pad(f, 4096 - isz, crc)

		with open("%s" % ld, "rb") as fp_read:
			s = fp_read.read()
			crc = binascii.crc32(s, crc) & 0xffffffff
			f.write(s)

		for i in range(len(fws)):
			with open("%s" % fws[i], "rb") as fp_read:
				s = fp_read.read()
				crc = binascii.crc32(s, crc) & 0xffffffff
				f.write(s)

		if cfg != "":
			with open("%s" % cfg, "rb") as fp_read:
				s = fp_read.read()
				crc = binascii.crc32(s, crc) & 0xffffffff
				f.write(s)

	else:
		# Section info: 16*section_num bytes
		for i in range(image.section_num):
			s = image.u_type.section[i].pack()
			crc = binascii.crc32(s, crc) & 0xffffffff
			f.write(s)

		# Section content
		for i in range(image.section_num - secure_section_num):
			with open('./section/%s' % section_name[i], 'rb') as fp_read:
				if section_align_pad[i] != 0:
					crc = file_pad(f, section_align_pad[i], crc)

				if include_zi_ni_bin == True or check_if_ni_zi(image.u_type.section[i].identifier) == False:
					s = fp_read.read()
					crc = binascii.crc32(s, crc) & 0xffffffff
					f.write(s)

		if sec_img:
			with open('keys_temp/pub_key.bin', 'rb') as fp_read:
				s = fp_read.read()
			crc = binascii.crc32(s, crc) & 0xffffffff
			f.write(s)

			crc = binascii.crc32(key_sha_buf, crc) & 0xffffffff
			f.write(key_sha_buf)

			crc = binascii.crc32(fw_sha_slice_buf, crc) & 0xffffffff
			f.write(fw_sha_slice_buf)

			crc = binascii.crc32(sha_sha_slice_buf, crc) & 0xffffffff
			f.write(sha_sha_slice_buf)

			crc = binascii.crc32(fw_sig_slice_buf, crc) & 0xffffffff
			f.write(fw_sig_slice_buf)

		# CPU config: 16 bytes
		crc = add_cpu_config(len(elfs), crc, fwc)

	# Reserve 0, alignd to 32 bytes
	if towrite - ori_towrite:
		crc = file_pad(f, towrite - ori_towrite, crc)

	padding_0 = 0x1DBE5236
	padding_1 = 0x20161201
	padding_2 = 0
	padding_3 = crc
	#print ("crc: " + hex(crc))

	# Padding: 16 bytes
	s = struct.pack("4I", padding_0, padding_1, padding_2, padding_3)
	towrite += PADDING_SZ
	f.write(s)

	if combine == 0:
		# Revision: 8 bytes
		for i in revision[0:8]:
			f.write(str.encode(i))
		# Nand info: 8 bytes
		for i in nand_info[0:8]:
			f.write(str.encode(i))
		towrite += (REVISON_SZ + NAND_INFO_SZ)

    # Security Revision
	if security_revision == True:
		sv_padding_0 = 0x56455253
		sv_padding_1 = 0x00000000
		sv_padding_2 = 0x00000000
		sv_padding_3 = 0x56455253

		# Padding: 16 bytes
		print("set Security Rev.")
		s = struct.pack("4I", sv_padding_0, sv_padding_1, sv_padding_2, sv_padding_3)
		towrite += PADDING_SZ
		f.write(s)

	before_align_towrite = towrite
	towrite = set_4K_aligned(towrite)

	# Reserve 0, aligned to 4K bytes
	if towrite - before_align_towrite:
		f.write(bytearray(towrite - before_align_towrite))

	f.close()
	shutil.rmtree('./section', ignore_errors=True)

	if sec_img:
                shutil.rmtree('./keys_temp', ignore_errors=True)

	print ('End OK .')
	sys.exit(0)

