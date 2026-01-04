#ifndef wordbank_h
#define wordbank_h

#define word_count 50
#define word_len 6  //takes into account \0 in addition to 5 chars

// extern declaration: tells other files this exists somewhere
extern const char word_bank[word_count][word_len];

#endif