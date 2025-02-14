# Deduplication file system build on FUSE

## compilation

- change storage place
modify ./src/def.h
```
#define BACKEND /path/to/storage // e.g. zns mount point
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
