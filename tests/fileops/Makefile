TEST_SOURCE=creat.c lseek.c stat.c dup.c fstat.c read.c write.c fread.c fwrite.c unlink.c mmap.c fork.c getdelim.c rename.c pread.c pwrite.c
TEST_EXE= creat lseek stat dup fstat read write fread fwrite unlink mmap fork getdelim rename pread pwrite

all: $(TEST_EXE) $(TEST_SOURCE)

creat: creat.c

lseek: lseek.c

stat: stat.c

dup: dup.c

fstat: fstat.c

read: read.c

write: write.c

fread: fread.c

fwrite: fwrite.c

unlink: unlink.c

mmap: mmap.c

fork: fork.c

getdelim: getdelim.c

rename: rename.c

pread: pread.c

pwrite: pwrite.c

clean:
	rm -f $(TEST_EXE) file_tmp
