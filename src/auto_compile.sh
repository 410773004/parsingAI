#!/usr/bin/expect -f


# You need to install the package first: sudo apt-get install expect

#RD version
set version D90
#other version
set version2 170

##### Compile for the first FW
spawn ./mpc-build.sh -k -v $version

# When the prompt appears, automatically enter your option
expect "1. Yes"
send "0\r"
sleep 1
# When the prompt appears, automatically enter your option
expect "1. No"
send "0\r"
sleep 1

# When the prompt appears, automatically enter your option
expect "Your input:"
send "2\r"
sleep 1
# When the prompt appears, automatically enter your option
expect "Your input: "
send "0\r"
sleep 1
# end
expect -timeout 40 "output4.map"
# Move the compiled FW bin file
spawn /bin/bash
send "rm -rf ../totalFW\r"
send "mkdir ../totalFW\r"
send "mv ../src-bin/FG2RD${version}_signed.fw ../totalFW/\r"
send "mv ../src-bin/sout_FG2RD${version}_signed.tar ../totalFW/\r"
expect eof



##### Compile for the second FW
spawn ./mpc-build.sh -k -v $version


expect "1. Yes"
send "0\r"
sleep 1

expect "1. No"
send "1\r"
sleep 1


expect "Your input:"
send "2\r"
sleep 1

expect "Your input: "
send "0\r"
sleep 1

expect -timeout 40 "output4.map"


spawn /bin/bash

send "mv ../src-bin/FH2RD${version}_signed.fw ../totalFW/\r"
send "mv ../src-bin/sout_FH2RD${version}_signed.tar ../totalFW/\r"
expect eof


##### Compile for the third FW
spawn ./mpc-build.sh -k -v $version2


expect "1. Yes"
send "0\r"
sleep 1

expect "1. No"
send "0\r"
sleep 1


expect "Your input:"
send "2\r"

expect "Your input: "
send "1\r"
sleep 1
# 結尾
expect -timeout 40 "output4.map"


spawn /bin/bash

send "mv ../src-bin/FG248${version2}_signed.fw ../totalFW/\r"
send "mv ../src-bin/sout_FG248${version2}_signed.tar ../totalFW/\r"

expect eof
