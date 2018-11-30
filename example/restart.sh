fusermount -u mountdir
cd ..
make
cd example
../src/sfs /tmp/assignment3/File-System-Manager/testfsfile mountdir
