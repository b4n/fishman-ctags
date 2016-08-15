#!/bin/sh

ctags_files=`make -f makefiles/list-translator-input.mak`
man_files=man/*.in.rst

#
# Generate templates for man pages
# foo.1.in.rst = [rst2man] => foo.1.in => [configure] => foo.1
#
rst2man=
if which rst2man > /dev/null; then
	rst2man=rst2man
elif which rst2man.py > /dev/null; then
	rst2man=rst2man.py
fi

if test x"${rst2man}" != x; then
    for i in ${man_files}; do
	o=${i%.rst}
	echo "rst2man converting $i to $o"
	${rst2man} $i $o
    done
else
    for i in ${man_files}; do
	o=${i%.rst}
	echo "use pre-converted file: $o"
    done
fi

misc/dist-test-cases > makefiles/test-cases.mak && \
    if autoreconf -vfi; then
	if which perl > /dev/null; then
	    for i in ${ctags_files}; do
		o=${i%.ctags}.c
		echo "optlib2c: translating $i to $o"
		./misc/optlib2c $i > $o
	    done
	else
	    for i in ${ctags_files}; do
		o=${i%.ctags}.c
		echo "use pre-translated file: $o"
	    done
	fi
    fi

exit $?
