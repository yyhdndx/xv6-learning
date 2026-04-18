#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4   // Symbolic link

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  /*
  知识点：软/硬链接
  硬链接计数：指向该 inode 的目录项数量
  当 nlink 减到 0 时，文件内容才会被真正删除
  注意：这里没有文件名，因为文件名属于目录项，
  一个文件可以有多个名字（硬链接），所以文件名
  不能存储在 stat 中。
  */
  uint64 size; // Size of file in bytes
};
