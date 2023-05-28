# get current path
cur_path=/mnt/d/Coding/OS-Homework/practice-3/libfuse/my_fuse  # '=' 两侧不能有空格，否则会当作命令

# clear previous FUSE if exists
cd ~/tmp
fusermount -u test_dir    # dis-mount previous FUSE

# compile
cd $cur_path
if ls -l fuse; then
  rm fuse
fi
gcc my_fuse.c rbtree.c -o fuse `pkg-config fuse3 --cflags --libs` -Wall
cp fuse ~/tmp/            # copy fuse to /tmp, so the file system can work

# switch into test directory
cd ~/tmp
./fuse test_dir           # mount the new FUSE
