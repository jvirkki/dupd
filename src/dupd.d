provider dupd {
  probe set_state_new(string, int, int);
  probe set_state_cache_done(string, int, int);
  probe set_state_need_data(string, int, int);
  probe set_state_done(string, int, int);
};
