#!/usr/bin/env python

import os
import argparse
import re
import subprocess
import sys
import hashlib
import shutil
import zlib

if sys.version_info[0] == 3:
	xrange = range

log_dictionary = {}
hitEvtidZero = False


def fileid_read(fn):
    h = open(fn, "r")
    for line in h:
        res = re.search('^#define __FILEID__ [a-z]+', line)
        if res != None:
            data = res.group().split(' ')
            return data[2]
    return "none"

def process_define(o, line, fid, evid):
    o.write("  #ifndef __trace_%d_%s_0\n" % (line, fid))
    o.write("  # define __trace_%d_%s_0(cat, level, evid) trace0(cat, level, 0x%04x)\n" % (line, fid, evid))
    o.write("  #endif\n")
    o.write("  #ifndef __trace_%d_%s_1\n" % (line, fid))
    o.write("  # define __trace_%d_%s_1(cat, level, evid, r0) trace1(cat, level, 0x%04x, r0)\n" % (line, fid, evid))
    o.write("  #endif\n")
    o.write("  #ifndef __trace_%d_%s_2\n" % (line, fid))
    o.write("  # define __trace_%d_%s_2(cat, level, evid, r0, r1) trace2(cat, level, 0x%04x, r0, r1)\n" % (line, fid, evid))
    o.write("  #endif\n")
    o.write("  #ifndef __trace_%d_%s_3\n" % (line, fid))
    o.write("  # define __trace_%d_%s_3(cat, level, evid, r0, r1, r2) trace3(cat, level, 0x%04x, r0, r1, r2)\n" % (line, fid, evid))
    o.write("  #endif\n")
    o.write("  #ifndef __trace_%d_%s_4\n" % (line, fid))
    o.write("  # define __trace_%d_%s_4(cat, level, evid, r0, r1, r2, r3) trace4(cat, level, 0x%04x, r0, r1, r2, r3)\n" % (line, fid, evid))
    o.write("  #endif\n")
    o.write("  #ifndef __trace_%d_%s_5\n" % (line, fid))
    o.write("  # define __trace_%d_%s_5(cat, level, evid, r0, r1, r2, r3, r4) trace5(cat, level, 0x%04x, r0, r1, r2, r3, r4)\n" % (line, fid, evid))
    o.write("  #endif\n")
    o.write("  #ifndef __trace_%d_%s_6\n" % (line, fid))
    o.write("  # define __trace_%d_%s_6(cat, level, evid, r0, r1, r2, r3, r4, r5) trace6(cat, level, 0x%04x, r0, r1, r2, r3, r4, r5)\n" % (line, fid, evid))
    o.write("  #endif\n")

def trace_cat_gen(fn, o, odir):
    paths = []
    paths_tmp = []
    for root, subdir, files in os.walk("."):
        if fn in sorted(files):
            paths_tmp.append(root)

    rtos_idx = 0xFFFF
    ftl_idx  = 0xFFFF
    for i in xrange(len(paths_tmp)):
        if "rtos" in paths_tmp[i]:
            rtos_idx = i
        elif "ftl" in paths_tmp[i]:
            ftl_idx  = i

    if 0xFFFF != rtos_idx:
        paths.append(paths_tmp[rtos_idx])
    if 0xFFFF != ftl_idx:
        paths.append(paths_tmp[ftl_idx])

    if (0xFFFF != rtos_idx) and (0xFFFF != ftl_idx):
        paths_tmp.remove(paths[0])
        paths_tmp.remove(paths[1])
    elif (0xFFFF != rtos_idx) or (0xFFFF != ftl_idx):
        paths_tmp.remove(paths[0])

    paths = paths + paths_tmp

    o.write("typedef enum {\n")
    for root in paths:
        with open(os.path.join(root, fn), 'r') as fin:
            for line in fin:
                res = re.search('[A-Z_]+_CAT', line)
                if res != None:
                    o.write("    %s,\n" % res.group())

    o.write("    MAX_TRACE,\n")
    o.write("} trace_cat_t;\n")

    return paths

def trace_modid_get(path):
    if "rtos" in path:
        modid = 0x0
    elif "ftl" in path:
        modid = 0x1
    else:
        modid = 0x2

    return modid

def trace_module_get(path):
    if "rtos" in path:
        return "RTOS"
    elif "ftl" in path:
        return "FTL"
    else:
        return "FW"

def trace_cat_get(path):
    cats = []
    with open(os.path.join(path, "trace-data"), 'r') as fin:
        for line in fin:
            res = re.search('[A-Z_]+_CAT', line)
            if res != None:
                cats.append(res.group())

    return cats

def string_to_hash(input_string):
    hash_value = hash(input_string)
    limit_hash_value = hash_value % 0xFFFF
    return limit_hash_value
   

fid_chk = {}
fid_chk_error = {}
def trace_evt_gen(paths, otrace, ofmts, odir, step):
    global fid_chk
    global fid_chk_error
    global hitEvtidZero
    global log_dictionary
    rr   = []
    fmts = []

    modid = trace_modid_get(paths[0])
    evid  = ((modid & 0xf) << 12) | 0x0

    for path in paths:
        cats  = trace_cat_get(path)
        if os.name == 'nt':
            cwd = os.getcwd()
            odir = os.path.abspath(odir)
            os.system("cd %s && dir /b/s *.c *.h | cscope -kRb -i- -f %s\cscope.out & cd %s" % (path, odir, cwd))
        else:
            os.system("find %s -name \*.c -o -name \*.h | cscope -kRb -i- -f %s/cscope.out" % (path, odir))

        for i in cats:
            r = i.split('_')
            m = r[0].lower()
            c = r[1].lower()
            trace_name = "%s_%s_trace" % (m, c)
            otrace.write("\n/* %s */\n" % (trace_name))
            otrace.write("#define %s(...) MKFN(%s,##__VA_ARGS__)\n" % (trace_name, trace_name))
            otrace.write("#define %s8(level, eventid, fmt, r0, r1, r2, r3, r4, r5) trace6(%s, level, eventid, (u32)(r0), (u32)(r1), (u32)(r2), (u32)(r3), (u32)(r4), (u32)(r5))\n" % (trace_name, i))
            otrace.write("#define %s7(level, eventid, fmt, r0, r1, r2, r3, r4) trace5(%s, level, eventid, (u32)(r0), (u32)(r1), (u32)(r2), (u32)(r3), (u32)(r4))\n" % (trace_name, i))
            otrace.write("#define %s6(level, eventid, fmt, r0, r1, r2, r3) trace4(%s, level, eventid, (u32)(r0), (u32)(r1), (u32)(r2), (u32)(r3))\n" % (trace_name, i))
            otrace.write("#define %s5(level, eventid, fmt, r0, r1, r2) trace3(%s, level, eventid, (u32)(r0), (u32)(r1), (u32)(r2))\n" % (trace_name, i))
            otrace.write("#define %s4(level, eventid, fmt, r0, r1) trace2(%s, level, eventid, (u32)(r0), (u32)(r1))\n" % (trace_name, i))
            otrace.write("#define %s3(level, eventid, fmt, r0) trace1(%s, level, eventid, (u32)(r0))\n" % (trace_name, i))
            otrace.write("#define %s2(level, eventid, fmt) trace0(%s, level, eventid)\n" % (trace_name, i))

            proc = subprocess.Popen(["cscope", "-d", "-L", "-0", "%s" % trace_name, "-f", "%s/cscope.out" % odir], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            done = False
            while done == False:
                line = proc.stdout.readline().strip()
                if line != b'':
                    # chechk error
                    res = re.search('trace', line)
                    if res == None:
                        print("cscope read line error:")
                        print(line)
                        exit(1)

                    # print(("fmt line: %s" %(line)))
                    fr = line.split(b'\"')
                    rr = line.split(b' ')
                    if os.name == 'nt':
                        fr[0] = fr[0].replace(os.getcwd()+'\\', '').replace('\\','/')
                        rr[0] = rr[0].replace(os.getcwd()+'\\', '').replace('\\','/')
                        if '/' not in rr[0]:
                            rr[0] = (path+'/'+rr[0]).replace('.\\','')
                    
                    # get evid in code
                    codeLine = " ".join(rr[3:])
                    idx = codeLine.find(trace_name)
                    if idx == -1:
                        print("\033[0;31;40m[ERROR] can't find trace_name %s:"%trace_name)
                        print(" ".join(rr))
                        print("\033[0m")
                        exit(1)
                    evid_string = codeLine[idx:].split(b',')[1].strip().lower()
                    if ((evid_string != '0') and ( evid_string[:2] != "0x" or "0x%04x"%int(evid_string, 16) != evid_string) or (int(evid_string, 16) > 0xffff)):
                        print("\033[0;31;40m[ERROR] Invalid evid, please set it to zero, or hex numbers less than 0xffff")
                        print(" ".join(rr))
                        print("\033[0m")
                        exit(1)
                    evid = int(evid_string, 16)

                    if(evid != 0):
                        if(step != 1):
                            if evid in log_dictionary:
                                print("\033[0;31;40m[ERROR] Same evid is already used")
                                print(codeLine)
                                print(log_dictionary[evid])
                                print("\033[0m")
                                exit(1)
                            log_dictionary[evid] = codeLine
                    else:
                        if(step == 0):
                            hitEvtidZero = True
                            pass
                        elif (step == 1):
                            f=open(rr[0],'r')
                            allLines=f.readlines()
                            f.close()
                            codeLine1 = allLines[int(rr[2])-1]
                            idx = codeLine1.find(trace_name)
                            if idx == -1:
                                print("\033[0;31;40m[ERROR] can't find trace_name %s:"%trace_name)
                                print(" ".join(rr))
                                print("\033[0m")
                                exit(1)
                            trace_line0 = codeLine1[:idx]
                            cc = codeLine1[idx:].split(b',')
                            hash_string = rr[1] + fr[1] # function + string
                            evid = string_to_hash(hash_string)
                            while evid in log_dictionary or evid == 0:
                                evid = string_to_hash(hash_string + str(evid))
                            log_dictionary[evid] = codeLine
                            cc[1] = " 0x%04x"%evid
                            allLines[int(rr[2])-1] = trace_line0 + (','.join(cc))
                            f=open(rr[0],'w+')
                            f.writelines(allLines)
                            f.close()
                    fid = fileid_read(rr[0])
                    if fid_chk.has_key(fid) == False:
                        fid_chk[fid] = rr[0]
                    else:
                        if fid_chk[fid] != rr[0] and fid_chk_error.has_key(rr[0]) == False:
                            fid_chk_error[rr[0]] = 1
                            print("\033[0;31;40m[ERROR] Multiple definition of __FILEID__ (%s) in %s and %s\033[0m"%(fid, fid_chk[fid], rr[0]))
                            
                    if len(fr) > 1:
                        fmts.append([rr[0], rr[1], rr[2], fr[1], line, fr[1], evid])
                    else:
                        fmts.append([rr[0], rr[1], rr[2], "todo", 0,  "todo", evid])
                        print("TODO:", rr, fr)
                else:
                    done = True;

    ostr = ""
    if(step != 1 and hitEvtidZero == False):
        for fmt in fmts:
            ostr = ostr + ("/* 0x%04x */{\"%s\", \"%s\", %s, \"%s\",},\\\n" % (fmt[6], fmt[0].decode('utf-8'), fmt[1].decode('utf-8'), int(fmt[2]), fmt[3].decode('utf-8')))

        ofmts.write(ostr)

    return ostr

def process_trace(fn, otrace, ofmts, odir, step):
    otrace.write("#pragma once\n")
    paths = trace_cat_gen(fn, otrace, odir)

    ofmts.write("\n\n#define DICTIONARY = { \\\n")
    ostr = ""
    #genernate trace for ./rtos
    if "rtos" in paths[0]:
        ostr = ostr + trace_evt_gen(paths[0 : 1], otrace, ofmts, odir, step)
        paths.remove(paths[0])

    #genernate trace for ./ftl
    if "ftl" in paths[0]:
        ostr = ostr + trace_evt_gen(paths[0 : 1], otrace, ofmts, odir, step)
        paths.remove(paths[0])

    #genernate trace for all other directory
    ostr = ostr + trace_evt_gen(paths, otrace, ofmts, odir, step)
    ofmts.write("};\n")

    otrace.write("\n\n#define EVID_FMT_CRC32 0x%08x\n\n" % (zlib.crc32(ostr.encode()) & 0xffffffff))
    ofmts.write("\n\n#define EVID_FMT_CRC32 0x%08x\n\n" % (zlib.crc32(ostr.encode()) & 0xffffffff))

parser = argparse.ArgumentParser()
parser.add_argument('-o', '--output')
args = parser.parse_args()

old_trace = args.output + "/trace-eventid.h"
new_trace = args.output + "/trace-eventid.h.tmp"

old_fmts = args.output + "/trace-fmtstr.h"
new_fmts = args.output + "/trace-fmtstr.h.tmp"

if(os.path.isfile(old_trace) and os.path.isfile(old_fmts)):
    pass
else:
    fp_trace = open(new_trace, "w")
    fp_fmts  = open(new_fmts, "w")
    process_trace("trace-data", fp_trace, fp_fmts, args.output, 0)
    if(hitEvtidZero) :
        process_trace("trace-data", fp_trace, fp_fmts, args.output, 1)
        hitEvtidZero = False
        log_dictionary = {}
        fp_trace.close()
        fp_fmts.close()
        fp_trace = open(new_trace, "w")
        fp_fmts  = open(new_fmts, "w")
        process_trace("trace-data", fp_trace, fp_fmts, args.output, 2)
    fp_trace.close()
    fp_fmts.close()
        

    hnew = hashlib.md5(open(new_trace, 'rb').read()).hexdigest()
    try:
        hold = hashlib.md5(open(old_trace, 'rb').read()).hexdigest()
    except IOError:
        hold = "none"

    if hnew != hold:
        shutil.copy(new_trace, old_trace)
        if os.path.exists("../qemu/hw/char"):
            shutil.copy(old_trace, "../qemu/hw/char/")

    hnew = hashlib.md5(open(new_fmts, 'rb').read()).hexdigest()
    try:
        hold = hashlib.md5(open(old_fmts, 'rb').read()).hexdigest()
    except IOError:
        hold = "none"

    if hnew != hold:
        shutil.copy(new_fmts, old_fmts)
        if os.path.exists("../qemu/hw/char"):
            shutil.copy(old_trace, "../qemu/hw/char/")
