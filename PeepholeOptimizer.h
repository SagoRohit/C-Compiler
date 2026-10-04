#pragma once
#include <string>
#include <vector>

// Simple text-level peephole optimizer over generated FASM assembly lines.
// Implements the four kinds of redundancy listed in the ICG spec (Section 2.2):
//   (i)   MOV a,b  ; MOV b,a          -> second MOV removed
//   (ii)  PUSH x   ; POP x            -> both removed
//   (iii) ADD/SUB reg,0  and  MOV reg,1 ; MUL reg  -> removed
//   (iv)  runs of consecutive labels  -> collapsed to one, jump targets rewritten
namespace Peephole {
    std::vector<std::string> optimize(const std::vector<std::string>& lines);
}
