# Deduplication file system build on FUSE

## compilation

- change storage place(**important**)

  setting ./src/def.h
  ```
  #define BACKEND /path/to/storage // e.g. zns mount point
  
  // comment/remove this line if you don't need to output mapping table in a file after FS umount
  #define MAPPING_OUTPUT_PATH "/home/johnnychang/result/mapping.txt"
  
  // comment/remove this line if you don't need to record read request in a file after FS umount
  #define READ_REQ_OUTPUT_PATH "/home/johnnychang/result/rdReq.txt"
  ```


- FastCDC mode
```
make
```

- CAFTL mode
```
make CAFTL
```

- No dedupe mode
```
make NoDedupe
```

- Debug Mode(fastCDC)
```
make debug
```

## start CDCFS
```
./CDCFS -f /path/to/FUSE/mount-point
```
