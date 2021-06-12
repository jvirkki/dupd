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

};
