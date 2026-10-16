// A call-shaped statement is deliberately outside the bounded node policy.
extern "C" unsigned source_node_unsupported(void (*callback)(unsigned), unsigned input) {
  unsigned flag[2];
  flag[0] = input;
  flag[1] = input + 1u;
  callback(flag[1]);
  return flag[0] + flag[1];
}
