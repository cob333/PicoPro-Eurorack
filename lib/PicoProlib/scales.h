// some of this code borrowed from https://github.com/alicedb2/tinyQuan

// Main scales with 1st note at LSB

#define MAJOR_PENTATONIC 0x295
#define MINOR_PENTATONIC 0x4a9
#define MAJOR 0xab5  // 0b101010110101
#define MINOR 0x5ad  // 0b10110101101
#define HARMONIC_MINOR 0x9ad
#define DORIAN 0x6ad
#define PHRYGIAN 0x5ab
#define LYDIAN 0xad5
#define MIXOLYDIAN 0x6b5
#define CHROMATIC 0xfff

uint16_t scales[] ={MAJOR_PENTATONIC,MAJOR,MINOR_PENTATONIC,MINOR,CHROMATIC,HARMONIC_MINOR,DORIAN,PHRYGIAN,LYDIAN,MIXOLYDIAN};
#define NUMSCALES sizeof(scales)/sizeof(uint16_t)

uint16_t rotate12left(uint16_t n, uint16_t d) {
  return 0xfff & ((n << (d % 12)) | (n >> (12 - (d % 12))));
}

uint16_t rotate12right(uint16_t n, uint16_t d) {
  return 0xfff & ((n >> (d % 12)) | (n << (12 - (d % 12))));
}

// quantize MIDI notes 0-128 to scale with given MIDI root note 0-128
uint8_t quantize(uint8_t note, uint16_t scale,uint8_t root){
  uint8_t n = note%12; // reduce to one octave
  uint8_t key = root%12;
  scale=rotate12left(scale,key); // adjust scale mask into the right key
  for (int i=0;i<3;++i) {  // quantize up, max 2 notes
    if (bitRead(scale,n)) return note;
    scale=rotate12right(scale,1);
    ++note;
  }
  return note; // failed to quantize - should not happen for most scales
}
