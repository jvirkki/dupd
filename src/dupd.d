provider dupd {

  // This marks the state of a file has changed.
  // See paths.h for the int constants for state values.
  // Parameters are:
  //   (string) path, (int) size, (int) previous state, (int) new state

  probe set_file_state(string, int, int, int);

  // This means the read buffer size has increased for this file.
  // Parameters are:
  //   (string) path, (int) size, (int) size_increase, (int) total

  probe readbuf_inc(string, int, int, int);

  // This means the read buffer size has decreased for this file.
  // Parameters are:
  //   (string) path, (int) size, (int) size_decrease, (int) total

  probe readbuf_dec(string, int, int, int);

  // This means the size_list structs allocation has increased for this size.
  // Parameters are:
  //   (int) filesize of the size_list, (int) size_increase, (int) total

  probe sizelist_inc(int, int, int);

  // This means the size_list structs allocation has decreased for this size.
  // Parameters are:
  //   (int) filesize of the size_list, (int) size_decrease, (int) total

  probe sizelist_dec(int, int, int);

  // This means the hashtable allocation has increased.
  // Parameters are:
  //   (int) size_increase, (int) total

  probe hashtable_inc(int, int);

  // This means the hashtable allocation has decreased.
  // Parameters are:
  //   (int) size_decrease, (int) total

  probe hashtable_dec(int, int);

  // This means the readlist allocation has increased.
  // Parameters are:
  //   (int) size_increase, (int) total

  probe readlist_inc(int, int);

  // This means the readlist allocation has decreased.
  // Parameters are:
  //   (int) size_decrease, (int) total

  probe readlist_dec(int, int);

  // This means the dirbuf allocation has increased.
  // Parameters are:
  //   (int) size_increase, (int) total

  probe dirbuf_inc(int, int);

  // This means the dirbuf allocation has decreased.
  // Parameters are:
  //   (int) size_decrease, (int) total

  probe dirbuf_dec(int, int);

  // This means the path block allocation has increased.
  // Parameters are:
  //   (int) size_increase, (int) total

  probe pblocks_inc(int, int);

  // This means the path block allocation has decreased.
  // Parameters are:
  //   (int) size_decrease, (int) total

  probe pblocks_dec(int, int);

};
