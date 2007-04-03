#! /bin/sh

# We change dir since the later utilities assume to work in the project dir
cd ${0%*/*}

# create config dir if not exist
test -d config || mkdir config

set -x
aclocal -I config
libtoolize --force --copy
autoheader
automake --foreign --add-missing --copy
autoconf
