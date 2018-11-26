fusermount -u mountdir
cd ..
make
cd example
../src/sfs /tmp/assignment\ 3/File-System-Manager/testfsfile mountdir
