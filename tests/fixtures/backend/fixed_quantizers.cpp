extern unsigned quantizer_observe(unsigned value);

unsigned fixed_quantizers(void* destination, const void* source, unsigned value) {
  asm {
    psq_l f0, 0(r4), 0, 0
    psq_st f0, 0(r3), 0, 0
    psq_l f0, 0(r4), 0, 1
    psq_st f0, 0(r3), 0, 1
    psq_l f0, 0(r4), 0, 2
    psq_st f0, 0(r3), 0, 2
    psq_l f0, 0(r4), 0, 3
    psq_st f0, 0(r3), 0, 3
    psq_l f0, 0(r4), 0, 4
    psq_st f0, 0(r3), 0, 4
    psq_l f0, 0(r4), 0, 5
    psq_st f0, 0(r3), 0, 5
    psq_l f0, 0(r4), 0, 6
    psq_st f0, 0(r3), 0, 6
    psq_l f0, 0(r4), 0, 7
    psq_st f0, 0(r3), 0, 7
  }
  return quantizer_observe(value);
}
