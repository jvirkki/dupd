provider dupd {

  // This marks the state of a file has changed.
  // See paths.h for the int constants for state values.
  // Parameters are:
  //   (string) path, (int) size, (int) previous state, (int) new state

  probe set_file_state(string, int, int, int);
};
