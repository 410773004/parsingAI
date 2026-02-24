#------------------------------------------------------------------------------
#                 Copyright(c) 2016-2019 Innogrit Corporation
#                             All Rights reserved.
#
#  The confidential and proprietary information contained in this file may
#  only be used by a person authorized under and to the extent permitted
#  by a subsisting licensing agreement from Innogrit Corporation.
#  Dissemination of this information or reproduction of this material
#  is strictly forbidden unless prior written permission is obtained
#  from Innogrit Corporation.
#------------------------------------------------------------------------------
cmf:
CMF files for encoder & decoder, and scrambler seed LUT

inc:
NCB HW register definitions header files

vendor:
Vendor specific settings, nand command format etc.
    vendor/mu:
	Micron specific

debug:
utilities that may be helpful for debugging in future.

test:
Test code to test NCL functions, may turn into validation code in future

To do list:
1. High priority

2. Normal priority

3. Low priority
DCC training for Micron
ZQ calibration
raw write into cache register for debug purpose
Histogram for Unic, Samsung, Hynix
FICU mode initialization
