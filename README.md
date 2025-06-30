# File Shredder
Arguments:
  file-shredder.exe *[FILENAME] *[MAX_THREAD] *[CHUNK_SIZE_MB]

Basically overwrites the file with zeroes and deletes it and the file becomes unrecoverable

It has fine multithreading but i do not using multithreading
I use it on 1 threads and 1MB of chunk size otherwise it lags like hell because its fast and eats up the resources
Currently can shred a 32GB file in 26 seconds
