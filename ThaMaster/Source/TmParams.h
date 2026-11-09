#pragma once

// Fck ThaMASTER : table unique des parametres (moteur, processeur, interface et presets s'appuient dessus).

namespace tmx
{
enum Module { MGlobal = 0, MSpec, MComp, MAna, MSt, MEq, MClip, MLim, kNumModules };
enum Kind { KFloat = 0, KBool, KChoice };

struct Def
{
    const char* id;
    const char* label;
    float lo, hi, def, step, skew;
    Kind kind;
    const char* unit;   // suffixe d'affichage ; pour KChoice : liste "a|b|c"
    int module;
};

//        enum        id            label        lo    hi    def   step  skew kind    unit        module
#define TM_LIST(X) \
    X(bypass,     "bypass",     "BYPASS",      0,    1,    0,    1,    1,   KBool,   "",         MGlobal) \
    X(inGain,     "inGain",     "INPUT",      -18,   18,   0,    0.1,  1,   KFloat,  " dB",      MGlobal) \
    X(outGain,    "outGain",    "OUTPUT",     -12,   12,   0,    0.1,  1,   KFloat,  " dB",      MGlobal) \
    X(matchOn,    "matchOn",    "MATCH",       0,    1,    0,    1,    1,   KBool,   "",         MGlobal) \
    X(monitor,    "monitor",    "MONITOR",     0,    2,    0,    1,    1,   KChoice, "None|Input|Delta", MGlobal) \
    X(target,     "target",     "TARGET",      0,    3,    2,    1,    1,   KChoice, "-14|-11|-9|-7", MGlobal) \
    X(specOn,     "specOn",     "ON",          0,    1,    1,    1,    1,   KBool,   "",         MSpec) \
    X(specAmt,    "specAmt",    "AMOUNT",      0,    100,  50,   1,    1,   KFloat,  "%",        MSpec) \
    X(compOn,     "compOn",     "ON",          0,    1,    1,    1,    1,   KBool,   "",         MComp) \
    X(compGlue,   "compGlue",   "GLUE",        0,    100,  35,   1,    1,   KFloat,  "%",        MComp) \
    X(compDetail, "compDetail", "DETAIL",      0,    100,  50,   1,    1,   KFloat,  "%",        MComp) \
    X(anaOn,      "anaOn",      "ON",          0,    1,    1,    1,    1,   KBool,   "",         MAna) \
    X(anaColor,   "anaColor",   "COLOR",       0,    100,  25,   1,    1,   KFloat,  "%",        MAna) \
    X(anaType,    "anaType",    "TYPE",        0,    3,    0,    1,    1,   KChoice, "Tape|Tube|Xfmr|Exciter", MAna) \
    X(stOn,       "stOn",       "ON",          0,    1,    1,    1,    1,   KBool,   "",         MSt) \
    X(stWidth,    "stWidth",    "WIDTH",       0,    200,  100,  1,    1,   KFloat,  "%",        MSt) \
    X(stLM,       "stLM",       "LOW MONO",    0,    1,    1,    1,    1,   KBool,   "",         MSt) \
    X(stLMFreq,   "stLMFreq",   "LM FREQ",     60,   300,  120,  1,    0.5, KFloat,  " Hz",      MSt) \
    X(eqOn,       "eqOn",       "ON",          0,    1,    1,    1,    1,   KBool,   "",         MEq) \
    X(eqBass,     "eqBass",     "BASS",       -6,    6,    0,    0.1,  1,   KFloat,  " dB",      MEq) \
    X(eqAir,      "eqAir",      "AIR",        -6,    6,    0,    0.1,  1,   KFloat,  " dB",      MEq) \
    X(clipOn,     "clipOn",     "ON",          0,    1,    1,    1,    1,   KBool,   "",         MClip) \
    X(clipGain,   "clipGain",   "GAIN",        0,    12,   0,    0.1,  1,   KFloat,  " dB",      MClip) \
    X(limOn,      "limOn",      "ON",          0,    1,    1,    1,    1,   KBool,   "",         MLim) \
    X(limGain,    "limGain",    "GAIN",        0,    15,   4,    0.1,  1,   KFloat,  " dB",      MLim) \
    X(limTP,      "limTP",      "TP",          0,    1,    1,    1,    1,   KBool,   "",         MLim) \
    X(limCeil,    "limCeil",    "CEILING",    -3,    0,   -1,    0.1,  1,   KFloat,  " dB",      MLim)

#define X(name, id, label, lo, hi, def, step, skew, kind, unit, mod) name,
enum P : int { TM_LIST(X) Count };
#undef X

#define X(name, id, label, lo, hi, def, step, skew, kind, unit, mod) \
    { id, label, (float) (lo), (float) (hi), (float) (def), (float) (step), (float) (skew), kind, unit, mod },
inline const Def kDefs[Count] = { TM_LIST(X) };
#undef X
} // namespace tmx
