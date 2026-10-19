// Ordinary expression statements; these are assignments, not declarations.
extern "C" unsigned source_node_writes(const void* condition, unsigned input) {
  unsigned flag[2];
  flag[0] = input;
  flag[1] = condition ? 0u : 1u;
  flag[1] = flag[0] + flag[1];
  return flag[1];
}
