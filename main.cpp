#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "antlr4-runtime.h"
#include "CSubsetLexer.h"
#include "CSubsetParser.h"
#include "CodeGenVisitor.h"
#include "PeepholeOptimizer.h"

using namespace antlr4;
using namespace std;

static const char* OUTDEC_PROC =
R"ASM(
; ---- OUTDEC: prints the signed 32-bit integer in EAX as decimal + newline ----
OUTDEC:
    PUSH EBX
    PUSH ECX
    PUSH EDX
    PUSH ESI
    OR   EAX, EAX
    JGE  OUTDEC_POSITIVE
    NEG  EAX
    PUSH EAX
    SUB  ESP, 4
    MOV  byte [ESP], '-'
    MOV  EAX, 4
    MOV  EBX, 1
    MOV  ECX, ESP
    MOV  EDX, 1
    INT  0x80
    ADD  ESP, 4
    POP  EAX
OUTDEC_POSITIVE:
    XOR  ECX, ECX
    MOV  EBX, 10
OUTDEC_DIGIT_LOOP:
    XOR  EDX, EDX
    DIV  EBX
    ADD  DL, 30h
    PUSH EDX
    INC  ECX
    TEST EAX, EAX
    JNZ  OUTDEC_DIGIT_LOOP
    MOV  ESI, ECX
    MOV  EBX, 1
    MOV  EDX, 1
OUTDEC_PRINT_LOOP:
    TEST ESI, ESI
    JZ   OUTDEC_NEWLINE
    MOV  EAX, 4
    MOV  ECX, ESP
    INT  0x80
    ADD  ESP, 4
    DEC  ESI
    JMP  OUTDEC_PRINT_LOOP
OUTDEC_NEWLINE:
    SUB  ESP, 4
    MOV  byte [ESP], 10
    MOV  EAX, 4
    MOV  ECX, ESP
    INT  0x80
    ADD  ESP, 4
    POP  ESI
    POP  EDX
    POP  ECX
    POP  EBX
    RET
)ASM";

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file.c>" << endl;
        return 1;
    }

    ifstream inputFile(argv[1]);
    if (!inputFile.is_open()) {
        cerr << "Error opening input file: " << argv[1] << endl;
        return 1;
    }

    vector<string> sourceLines;
    {
        ifstream lineFile(argv[1]);
        string line;
        while (getline(lineFile, line)) sourceLines.push_back(line);
    }

    ANTLRInputStream input(inputFile);
    CSubsetLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    CSubsetParser parser(&tokens);

    CSubsetParser::StartContext* tree = parser.start();
    if (parser.getNumberOfSyntaxErrors() > 0) {
        cerr << "Syntax errors found; ICG assumes an error-free input program "
             << "(errors should already be caught by the syntax/semantic analysis phase)." << endl;
        return 1;
    }

    CodeGenVisitor visitor(sourceLines);
    visitor.visit(tree);

    if (!visitor.sawMain) {
        // Keep the assembled program valid/runnable even if the source has no main().
        visitor.codeSeg << "main:\n"
                         << "    PUSH EBP\n"
                         << "    MOV  EBP, ESP\n"
                         << "    MOV EAX, 1\n"
                         << "    XOR EBX, EBX\n"
                         << "    INT 0x80\n\n";
    }

    ostringstream finalAsm;
    finalAsm << "; Online ICG B2\n"
             << "format ELF executable 3\n"
             << "entry main\n\n"
             << "segment readable writeable\n"
             << visitor.dataSeg.str() << "\n"
             << "segment readable executable\n"
             << visitor.codeSeg.str()
             << OUTDEC_PROC;

    {
        ofstream out("code.asm");
        out << finalAsm.str();
    }

    vector<string> lines;
    {
        istringstream iss(finalAsm.str());
        string l;
        while (getline(iss, l)) lines.push_back(l);
    }
    vector<string> optimized = Peephole::optimize(lines);
    {
        ofstream out("optimized_code.asm");
        for (auto& l : optimized) out << l << "\n";
    }

    cout << "Generated code.asm (" << lines.size() << " lines) and optimized_code.asm ("
         << optimized.size() << " lines)." << endl;
    return 0;
}
