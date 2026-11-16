#include "game/FamilyApi.hpp"

// The program's ID (FamilyApi.hpp): tools/program_id.py writes it over this
// placeholder once the program is linked, as part of the link (build.py).
// Defined here alone, and read elsewhere only through its declaration, so
// that nothing is compiled with the placeholder's value in place of it.
extern "C" __attribute__((used)) const unsigned char hlProgramId[hlrt::kProgramIdBytes] = {'H', 'L', 'P', 'R',
                                                                                         'O', 'G', 'I', 'D'};
