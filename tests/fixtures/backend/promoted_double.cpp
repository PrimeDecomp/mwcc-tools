extern unsigned observe_word(unsigned);
extern double observe_double(double);

double promoted_double(unsigned condition, double value) {
  double flag[2];
  int k;
  for (k = 1; k < 2; ++k) flag[k] = condition ? 0.0 : 1.0;
  if (k > 0) observe_word(condition + 7);
  if (flag[1]) flag[1] = observe_double(value);
  return flag[1] + observe_double(value + 2.0) + flag[1];
}
