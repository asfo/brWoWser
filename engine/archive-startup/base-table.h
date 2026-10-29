/* Independent portable table transcribed from retained reference metadata. */
static const struct Base {
  const char *name;
  U category, mask;
} bases[28] = {
    {"alternate.MPQ", 3, 3},
    {"interface.MPQ", 1, 1},
    {"misc.MPQ", 1, 1},
    {"model.MPQ", 1, 1},
    {"texture.MPQ", 1, 1},
    {"terrain.MPQ", 1, 1},
    {"wmo.MPQ", 1, 1},
    {"sound.MPQ", 1, 1},
    {"fonts.MPQ", 1, 1},
    {"dbc.MPQ", 1, 1},
    {"speech.MPQ", 1, 1},
    {"expansionloc.MPQ", 2, 1},
    {"lichkingloc.MPQ", 2, 1},
    {"expansionspeech.MPQ", 2, 1},
    {"lichkingspeech.MPQ", 2, 1},
    {"expansion.MPQ", 2, 3},
    {"lichking.MPQ", 2, 3},
    {"common.MPQ", 1, 2},
    {"common-2.MPQ", 2, 2},
    {"****\\locale-****.MPQ", 1, 2},
    {"****\\speech-****.MPQ", 1, 2},
    {"****\\expansion-locale-****.MPQ", 2, 2},
    {"****\\lichking-locale-****.MPQ", 2, 2},
    {"****\\expansion-speech-****.MPQ", 2, 2},
    {"****\\lichking-speech-****.MPQ", 2, 2},
    {"development.MPQ", 2, 1},
    {"streaming.MPQ", 4, 1},
    {"streamingloc.MPQ", 4, 1},
};
