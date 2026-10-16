// Two actual arrays and nested calls exercise actual inline substitution.
static inline unsigned source_increment(unsigned value) { return value + 1u; }
static inline void source_assign(unsigned* target, unsigned value) {
  target[1] = value;
}
extern "C" unsigned source_node_inline_arrays(unsigned input) {
  unsigned left[2];
  unsigned right[2];
  left[0] = input;
  right[0] = input + 1u;
  source_assign(left, source_increment(input));
  source_assign(right, source_increment(source_increment(input)));
  return left[0] + left[1] + right[0] + right[1];
}
